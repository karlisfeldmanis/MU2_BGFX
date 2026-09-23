#include "gfx/renderer.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

#include "core/files.h"
#include "core/log.h"
#include "gfx/views.h"

namespace mu::gfx {

void Renderer::bindShadeInputs() {
    bgfx::setUniform(uSunDir_, shade_.sunDir);
    bgfx::setUniform(uSunColour_, shade_.sunColour);
    bgfx::setUniform(uSkyColour_, shade_.skyColour);
    bgfx::setUniform(uGroundColour_, shade_.groundColour);
    // A probe face sees the town from inside the air too, and its reflection is of the
    // town as the eye sees it; but the probe stands a few metres from what it holds, so
    // the dust it draws is the near air's, which is little.
    bgfx::setUniform(uDust_, shade_.dust);
    if (probePass_) {
        // A probe face is lit for its own eye, not the camera's.
        const float eye[4] = {probeAt_[0], probeAt_[1], probeAt_[2], shade_.camPos[3]};
        bgfx::setUniform(uCamPos_, eye);
    } else {
        bgfx::setUniform(uCamPos_, shade_.camPos);
    }
    bgfx::setUniform(uParams_, shade_.params);
    bgfx::setUniform(uShadowMtx_, shade_.shadowMtx);
    bgfx::setUniform(uShadowParams_, shade_.shadowParams);
    bgfx::setUniform(uShadowDebug_, shade_.shadowDebug);
    bgfx::setUniform(uShadowReach_, shade_.shadowReach);
    bgfx::setTexture(4, sShadowCompare_, shadowMap_,
                     BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(5, sShadowDepth_, shadowMap_,
                     BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(7, sAo_, probePass_ ? whiteAo_ : blurTex_);
    // The probe: read by the camera's shade once one has been filtered, and never by a probe
    // face, which takes the closed-form sky instead of the cube it is being drawn into.
    if (bgfx::isValid(uProbe_)) {
        const bool read = probeOk_ && probeOn_ && probeReady_ && !probePass_;
        const float probe[4] = {read ? 1.0f : 0.0f, float(kProbeMips - 1),
                                read ? probeView_ : 0.0f, metalGain_};
        bgfx::setUniform(uProbe_, probe);
        bgfx::setUniform(uProbePos_, probeTaken_);
        bgfx::setTexture(15, sProbe_, read ? probeFiltered_ : blackCube_);
    }
    bgfx::setUniform(uLampGrid_, lampGridUniform_);
    bgfx::setUniform(uLampParams_, lampParams_);
    // Only when there are any. This runs on EVERY shaded draw in the frame, so the common
    // case -- nothing burning -- must not pay two uniform uploads a draw for an array the
    // shader's loop is about to not read. lampParams_.z is 0 then, and that is what stops it.
    if (transientCount_ > 0) {
        bgfx::setUniform(uTransientAt_, transientAt_, kMaxTransientLights);
        bgfx::setUniform(uTransientColour_, transientColour_, kMaxTransientLights);
    }
    bgfx::setTexture(13, sLamps_, lamps_);
    bgfx::setTexture(14, sLampGrid_, lampGrid_);
}

void Renderer::fitSplit(const Camera& camera, const Lighting& lighting, const float* worldAxes,
                        float* side, bx::Vec3* offset) {
    // Relative to the target throughout, so the answer depends on the camera's shape and not
    // on where in the world it stands.
    const bx::Vec3 eye(camera.position[0] - camera.target[0],
                       camera.position[1] - camera.target[1],
                       camera.position[2] - camera.target[2]);
    const bx::Vec3 forward = bx::normalize(bx::neg(eye));
    const bx::Vec3 right =
        bx::normalize(bx::cross(forward, bx::Vec3(camera.up[0], camera.up[1], camera.up[2])));
    const bx::Vec3 upward = bx::cross(right, forward);
    const float tanY = std::tan(camera.fovDegrees * 0.5f * 3.14159265f / 180.0f);
    const float tanX = tanY * float(width_) / float(std::max(1, height_));
    const float plane = -lighting.shadowFitBelow;

    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    auto take = [&](const bx::Vec3& p) {
        // worldAxes' translation runs along the sun, so x and y are the rotation's alone.
        const bx::Vec3 l = bx::mul(p, worldAxes);
        lo[0] = std::min(lo[0], l.x);
        hi[0] = std::max(hi[0], l.x);
        lo[1] = std::min(lo[1], l.y);
        hi[1] = std::max(hi[1], l.y);
    };
    take(eye);
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const bx::Vec3 ray = bx::add(forward, bx::add(bx::mul(right, float(sx) * tanX),
                                                          bx::mul(upward, float(sy) * tanY)));
            // A corner that looks level or up never meets the ground, and there is no hull
            // to fit: the fixed square it was given stands.
            if (ray.y > -1e-3f) return;
            take(bx::add(eye, bx::mul(ray, (plane - eye.y) / ray.y)));
        }
    }
    // A twentieth over, for the filter's reach past the box's own edge.
    const float wanted = std::max(hi[0] - lo[0], hi[1] - lo[1]) * 1.05f;
    if (fittedSide_ <= 0.0f || std::fabs(wanted - fittedSide_) > 0.02f * fittedSide_) {
        fittedSide_ = wanted;
        core::logf("shadow split fitted to the camera: %.1f m, %.1f mm a texel", fittedSide_,
                   1000.0f * fittedSide_ / float(shadowSize_));
    }
    *side = fittedSide_;

    // The box's centre, back out of the sun's axes into the world.
    float fromLight[16];
    bx::mtxInverse(fromLight, worldAxes);
    const bx::Vec3 centre((lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, 0.0f);
    *offset = bx::sub(bx::mul(centre, fromLight), bx::mul(bx::Vec3(0.0f, 0.0f, 0.0f), fromLight));
}

void Renderer::resetPalettes() {
    // Row 0 is the bind row, and it is written every frame rather than once at init because
    // the upload below sends only the rows this frame wrote: a row written once at start-up
    // would never be uploaded at all.
    paletteWritten_ = 0;
    paletteRefused_ = 0;
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float rows[12];
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 4; ++row) rows[column * 4 + row] = identity[row * 4 + column];
    }
    for (int bone = 0; bone < kMaxBones; ++bone) {
        std::memcpy(&paletteCpu_[size_t(bone) * 12], rows, sizeof(rows));
    }
    paletteWritten_ = 1;
}

int Renderer::addPalette(const float* rows12, int bones) {
    if (paletteWritten_ >= kMaxPaletteRows) {
        ++paletteRefused_;
        return -1;
    }
    const int row = paletteWritten_++;
    // A rig too big for the palette is clamped rather than refused: the bones past the end
    // stay whatever the row held, which is visible, where a silent return of -1 would put
    // the whole figure in bind pose and look like a missing clip. Nothing in this content
    // has more than 60; the lobby's faces, at 100 to 115, would be the first.
    // A caller that hands over more rows than fit is clamped here as well as at the pose,
    // because this is the one that owns the texture's width.
    if (bones > kMaxBones) bones = kMaxBones;
    std::memcpy(&paletteCpu_[size_t(row) * kMaxBones * 12], rows12,
                size_t(bones) * 12 * sizeof(float));
    return row;
}

void Renderer::submitBatches(bgfx::ViewId view, bgfx::ProgramHandle program,
                             bgfx::ProgramHandle skinnedProgram,
                             const std::vector<Batch>& batches, const bgfx::InstanceDataBuffer& idb,
                             uint64_t state, bool bindMaterial, bool glowPass) {
    for (const Batch& batch : batches) {
        const content::Mesh& mesh = *batch.mesh;
        const bool skinned = mesh.isSkinned();
        const bgfx::ProgramHandle batchProgram = skinned ? skinnedProgram : program;
        for (const content::Part& part : mesh.parts()) {
            const content::Material& material = mesh.materials()[part.material];
            // A glow is drawn in its own pass and in no other. See the header.
            if (material.glow != glowPass) continue;
            // Nor grass, flowers or leaves: hundreds of small cutout draws that, at the blur a
            // reflection is read at, are the green the ground under them already gives.
            if (probePass_ && (batch.posed || material.translucency > 0.0f)) continue;

            // A cutout discards in every pass, this one included, or a leaf casts a card.
            // z and w are glTF's roughness and metal factors, which the shade pass multiplies
            // the ORM by: a material with no ORM map carries its whole answer there. A glow
            // has neither, and its z is the sheet's glow_strength instead; fs_glow never
            // reads w as a metal factor, so a glow's w instead carries how far MoveObject's
            // BlendMeshTexCoordV has slid its one additive submesh -- the world clock times
            // the material's own scroll rate, wrapped to a fraction the way MU's own
            // -(WorldTime % 1000) * 0.001 does, negative so the sheet slides down and the
            // waterspout's water falls rather than climbs. 0 on every glow that does not
            // scroll, which reads as no offset at all.
            // y carries two flags: 1 two-sided, 2 calibrated (the albedo's metal is already
            // reflectance, so the sheet's metal_gain stays off it). fs_shade unpacks them.
            const float scrollOffset = -std::fmod(elapsed_ * material.scrollPerSecond, 1.0f);
            const float materialParams[4] = {material.cutout,
                                             (material.twoSided ? 1.0f : 0.0f) +
                                                 (material.calibrated ? 2.0f : 0.0f),
                                             glowPass ? glowStrength_ : material.roughnessFactor,
                                             glowPass ? scrollOffset : material.metalFactor};
            bgfx::setUniform(uMaterial_, materialParams);
            // The albedo is bound even in the depth passes, because the cutout reads its alpha.
            bgfx::setTexture(0, sAlbedo_, material.albedo);
            if (bindMaterial) {
                bgfx::setTexture(1, sNormal_, material.normal);
                bgfx::setTexture(2, sOrm_, material.orm);
                bgfx::setTexture(3, sEmissive_, material.emissive);
                const float translucency[4] = {material.translucency, 0.0f, 0.0f, 0.0f};
                bgfx::setUniform(uTranslucency_, translucency);
                bindShadeInputs();
            }

            uint64_t drawState = state;
            // MU's figures are single sheets of mixed winding and are drawn two-sided. A glow
            // always is: MU draws its BlendMesh with culling off, and a flame card seen from
            // behind is still a flame.
            if (!material.twoSided && !glowPass) drawState |= cullBit_;
            // No alpha to coverage here, deliberately. It was set for a while and did
            // nothing: coverage comes from gl_FragColor.a and every pass writes 1.0 or a
            // depth, so the mask was always full. Making it real is not one line -- the
            // prepass and the shade pass must compute the SAME mask or the shade pass's
            // DEPTH_TEST_EQUAL leaves the samples it drops unshaded and every leaf grows a
            // black fringe, and the prepass has no channel free for coverage until its
            // normal is packed octahedrally. That is sprint 3's work, with the grass that
            // needs it and can test it. See docs/conventions.md.

            // Per draw, like the shadow map and the AO above and for the same reason:
            // bgfx::submit discards its bindings, so a palette bound once a view reaches
            // the first figure and leaves every other one reading an unbound stage -- which
            // is a pose of zeroes, and a figure collapsed into a point at the origin.
            if (skinned) bgfx::setTexture(12, sBones_, palette_);

            bgfx::setVertexBuffer(0, mesh.vertexBuffer());
            bgfx::setIndexBuffer(mesh.indexBuffer(), part.firstIndex, part.indexCount);
            bgfx::setInstanceDataBuffer(&idb, batch.first, batch.count);
            bgfx::setState(drawState);
            bgfx::submit(view, batchProgram);
            ++drawCount_;
        }
    }
}

void Renderer::bloom(const Lighting& lighting) {
    if (!bgfx::isValid(bloomDownProgram_) || !bgfx::isValid(bloomUpProgram_)) return;
    // Down: the shade target into level 0 with the threshold, then each level into the next.
    for (int i = 0; i < kBloomLevels; ++i) {
        const bgfx::ViewId view = bgfx::ViewId(ViewBloomDown + i);
        const uint16_t sourceW = i == 0 ? uint16_t(width_) : bloomW_[i - 1];
        const uint16_t sourceH = i == 0 ? uint16_t(height_) : bloomH_[i - 1];
        bgfx::setViewFrameBuffer(view, bloomFb_[i]);
        bgfx::setViewRect(view, 0, 0, bloomW_[i], bloomH_[i]);
        bgfx::setViewClear(view, 0, 0, 1.0f, 0);
        bgfx::setViewTransform(view, nullptr, nullptr);
        const float params[4] = {lighting.bloomThreshold, std::max(lighting.bloomKnee, 1e-3f),
                                 i == 0 ? 1.0f : 0.0f, 0.0f};
        const float texel[4] = {1.0f / float(sourceW), 1.0f / float(sourceH), 0.0f, 0.0f};
        bgfx::setUniform(uBloom_, params);
        bgfx::setUniform(uBloomTexel_, texel);
        bgfx::setTexture(8, sColour_, i == 0 ? shadeColour_ : bloomTex_[i - 1]);
        screenPass(view, bloomDownProgram_);
    }
    // Up: each level read through a tent and ADDED into the one above it, so level 0 ends up
    // holding every level's share, the widest softest ones included.
    for (int j = 0; j < kBloomLevels - 1; ++j) {
        const int target = kBloomLevels - 2 - j;
        const bgfx::ViewId view = bgfx::ViewId(ViewBloomUp + j);
        bgfx::setViewFrameBuffer(view, bloomFb_[target]);
        bgfx::setViewRect(view, 0, 0, bloomW_[target], bloomH_[target]);
        bgfx::setViewClear(view, 0, 0, 1.0f, 0);
        bgfx::setViewTransform(view, nullptr, nullptr);
        const float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float texel[4] = {1.0f / float(bloomW_[target + 1]),
                                1.0f / float(bloomH_[target + 1]), 0.0f, 0.0f};
        bgfx::setUniform(uBloom_, params);
        bgfx::setUniform(uBloomTexel_, texel);
        bgfx::setTexture(8, sColour_, bloomTex_[target + 1]);
        bgfx::setVertexBuffer(0, screenVb_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD);
        bgfx::submit(view, bloomUpProgram_);
        ++drawCount_;
    }
}

void Renderer::submitGround(bgfx::ViewId view, bgfx::ProgramHandle program,
                            const content::Ground& g, uint64_t state, bool lit) {
    for (const content::GroundPart& part : g.parts()) {
        if (lit) {
            const float repeat[4] = {part.base.repeat, part.overlay.repeat, part.base.relief,
                                     part.overlay.relief};
            bgfx::setUniform(uGroundRepeat_, repeat);
            // The bite is MU2's own 0.35, and the second component says whether this surface
            // has an overlay at all: nine of Lorencia's forty-four are a base standing alone,
            // and those must not blend against a texture nothing meaningful is bound to.
            //
            // z and w are each layer's water slide, in widths of its own sheet: MuMain's
            // WaterMove, `(WorldTime % 20000) * 0.00005` (ZzzLodTerrain.cpp), added to U on
            // every tile that wears TileWater01 -- one sheet width every twenty seconds, along
            // the columns, the same on every water tile so the river moves as one.
            const float slide = std::fmod(elapsed_, 20.0f) * 0.05f;
            const float blend[4] = {0.35f, part.hasOverlay ? 1.0f : 0.0f,
                                    part.base.water ? slide : 0.0f,
                                    part.overlay.water ? slide : 0.0f};
            bgfx::setUniform(uGroundBlend_, blend);
            bgfx::setTexture(0, sAlbedo_, part.base.albedo);
            bgfx::setTexture(1, sNormal_, part.base.normal);
            bgfx::setTexture(2, sOrm_, part.base.orm);
            bgfx::setTexture(9, sAlbedo2_, part.overlay.albedo);
            bgfx::setTexture(10, sNormal2_, part.overlay.normal);
            bgfx::setTexture(11, sOrm2_, part.overlay.orm);
            bindShadeInputs();
        }
        bgfx::setVertexBuffer(0, g.vertexBuffer());
        bgfx::setIndexBuffer(g.indexBuffer(), part.firstIndex, part.indexCount);
        // The land is the first single-sided surface in the project -- every material in
        // MU2's build is double sided -- so this is the one place a winding or a handedness
        // mistake shows as a hole rather than as nothing at all.
        bgfx::setState(state | cullBit_);
        bgfx::submit(view, program);
        ++drawCount_;
    }
}

void Renderer::screenPass(bgfx::ViewId view, bgfx::ProgramHandle program) {
    bgfx::setVertexBuffer(0, screenVb_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(view, program);
    ++drawCount_;
}

void Renderer::cameraMatrices(const Camera& camera, float* view, float* proj) const {
    bx::mtxLookAt(view, bx::Vec3(camera.position[0], camera.position[1], camera.position[2]),
                  bx::Vec3(camera.target[0], camera.target[1], camera.target[2]),
                  bx::Vec3(camera.up[0], camera.up[1], camera.up[2]), bx::Handedness::Right);
    bx::mtxProj(proj, camera.fovDegrees, float(width_) / float(height_), camera.nearPlane,
                camera.farPlane, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
}

void Renderer::draw(const Camera& camera, const Lighting& lighting,
                    const std::vector<Drawable>& drawables, const content::Ground* ground,
                    const std::vector<Drawable>* casters) {
    drawCount_ = 0;
    // The ground has no cutout, and fs_shadow and fs_ground_prepass read this to know it.
    const float noCutout[4] = {-1.0f, 0.0f, 0.0f, 0.0f};

    // The frame's poses go up in one upload, and only the rows the game actually filled:
    // thirty-one figures at sixty bones is 95 kB, where the whole texture is 1.6 MB. Every
    // pass that follows reads them, the shadow pass included -- a figure casts the pose it
    // is in, not the pose it was bound in.
    if (bgfx::isValid(palette_) && paletteWritten_ > 0) {
        const uint32_t bytes = uint32_t(paletteWritten_) * kMaxBones * 12 * uint32_t(sizeof(float));
        bgfx::updateTexture2D(palette_, 0, 0, 0, 0, uint16_t(kMaxBones * 3),
                              uint16_t(paletteWritten_), bgfx::copy(paletteCpu_.data(), bytes));
    }

    // The lights' flicker, when it moved: two rows of the 256-wide texture, 8 kB.
    if (lampsDirty_ && bgfx::isValid(lamps_)) {
        bgfx::updateTexture2D(lamps_, 0, 0, 0, 0, uint16_t(kMaxPointLights + 1), 2,
                              bgfx::copy(lampCpu_.data(), uint32_t(lampCpu_.size() * sizeof(float))));
        lampsDirty_ = false;
    }
    lampParams_[0] = lighting.lampStrength;
    glowStrength_ = lighting.glowStrength;
    probeOn_ = lighting.probe > 0.5f;
    probeView_ = lighting.probeView;
    // Switched off, the cube is forgotten: switched back on, it starts from a whole new one
    // rather than reading one taken wherever the player stood when it went off.
    if (!probeOn_) {
        probeReady_ = probeFilterDue_ = false;
        probeNextFace_ = 0;
    }
    metalGain_ = lighting.metalGain;

    // --- the camera -------------------------------------------------------------------
    // Right-handed, said out loud. bx defaults every one of these to Handedness::Left, and
    // a left-handed view mirrors the frame left to right and makes view-space z positive in
    // front of the eye -- which silently turned the whole prepass depth negative and made
    // the SSAO take its "this is sky" path on every pixel of the screen. docs/conventions.md
    // says right-handed; this is where that has to be enforced.
    float view[16];
    float proj[16];
    cameraMatrices(camera, view, proj);
    const bool homogeneous = bgfx::getCaps()->homogeneousDepth;

    // --- the batches, and one instance buffer the whole frame reads -------------------
    // Two lists share it: what the camera draws, and what the sun's split draws. They are
    // usually the same instances, and they are not the same when the camera's chunks have
    // been culled -- a chunk behind the camera still casts into the frame.
    batches_.clear();
    casterBatches_.clear();
    const std::vector<Drawable>& casterList = casters ? *casters : drawables;
    const bool anything = !drawables.empty() || !casterList.empty() || ground != nullptr;
    if (anything) {
        // Grouped by mesh, keeping the order each mesh was first seen in, so a frame's draw
        // order does not shuffle between runs and a measurement stays comparable.
        std::vector<std::vector<const Drawable*>> groups;
        std::vector<std::vector<const Drawable*>> casterGroups;
        // What is only part there, kept out of the opaque passes and drawn after them. Almost
        // always empty: it is the character's entrance and nothing else so far.
        std::vector<std::vector<const Drawable*>> fadeGroups;
        std::vector<Batch> fadeBatches;
        auto group = [](const std::vector<Drawable>& list,
                        std::vector<std::vector<const Drawable*>>& out,
                        std::vector<Batch>& batches, bool fading = false) {
            std::unordered_map<const content::Mesh*, size_t> seen;
            out.reserve(8);
            for (const Drawable& d : list) {
                if (!d.mesh) continue;
                if ((d.fade < 1.0f) != fading) continue;
                auto found = seen.find(d.mesh);
                if (found == seen.end()) {
                    seen.emplace(d.mesh, out.size());
                    out.emplace_back();
                    out.back().push_back(&d);
                    batches.push_back(Batch{d.mesh, 0, 0, d.paletteRow >= 0 || !d.inProbe});
                } else {
                    out[found->second].push_back(&d);
                    if (d.paletteRow >= 0 || !d.inProbe) batches[found->second].posed = true;
                }
            }
        };
        group(drawables, groups, batches_);
        group(drawables, fadeGroups, fadeBatches, true);
        const bool separateCasters = casters != nullptr;
        if (separateCasters) group(casterList, casterGroups, casterBatches_);

        // A 4x4 matrix, the instance's baked light, and the row its pose occupies in the
        // bone palette. The depth passes read the matrix and the row and skip the light,
        // which costs them nothing: the stride is what the buffer is walked by, not what
        // each shader reads.
        const uint32_t stride = 96;
        uint32_t total = 0;
        for (const auto& g : groups) total += uint32_t(g.size());
        for (const auto& g : fadeGroups) total += uint32_t(g.size());
        for (const auto& g : casterGroups) total += uint32_t(g.size());

        // bgfx will hand back fewer than asked for if the transient buffer is full. Asking
        // first and checking is the difference between a short frame and a corrupt one.
        const uint32_t available = bgfx::getAvailInstanceDataBuffer(total, stride);
        if (available < total) {
            core::logError("the instance buffer holds %u of %u instances this frame", available,
                           total);
            total = available;
        }
        if (total > 0 || ground) {
            // Allocated only when something wants it: the land is in world space already and
            // has no instances at all.
            bgfx::InstanceDataBuffer idb = {};
            if (total > 0) bgfx::allocInstanceDataBuffer(&idb, total, stride);
            uint32_t written = 0;
            auto fill = [&](std::vector<std::vector<const Drawable*>>& from,
                            std::vector<Batch>& batches) {
                for (size_t gi = 0; gi < from.size() && total > 0; ++gi) {
                    batches[gi].first = written;
                    uint32_t count = 0;
                    for (const Drawable* d : from[gi]) {
                        if (written >= total) break;
                        std::memcpy(idb.data + written * stride, d->transform,
                                    sizeof(float) * 16);
                        std::memcpy(idb.data + written * stride + sizeof(float) * 16, d->light,
                                    sizeof(float) * 4);
                        // No row of its own means the bind row, which is row 0 and is the
                        // identity. -1 would be read as a texel outside the palette.
                        const float skin[4] = {
                            float(d->paletteRow < 0 ? kBindRow : d->paletteRow), d->fade, 0.0f,
                            0.0f};
                        std::memcpy(idb.data + written * stride + sizeof(float) * 20, skin,
                                    sizeof(skin));
                        ++written;
                        ++count;
                    }
                    batches[gi].count = count;
                }
                batches.erase(std::remove_if(batches.begin(), batches.end(),
                                             [](const Batch& b) { return b.count == 0; }),
                              batches.end());
            };
            fill(groups, batches_);
            fill(fadeGroups, fadeBatches);
            if (separateCasters) fill(casterGroups, casterBatches_);
            // Without a list of its own, the sun draws what the camera draws.
            std::vector<Batch>& shadowBatches = separateCasters ? casterBatches_ : batches_;

            // --- view 0: the sun's split ---------------------------------------------
            float sunDir[3];
            lighting.sunDirection(sunDir);

            // Straight down would make the up vector parallel to the view; +z serves then.
            const bx::Vec3 up = std::fabs(sunDir[1]) > 0.99f ? bx::Vec3(0.0f, 0.0f, 1.0f)
                                                             : bx::Vec3(0.0f, 1.0f, 0.0f);
            // The sun's own axes through the WORLD's origin: the one frame in which a grid of
            // texels stays nailed to the ground. Its translation runs along the sun, so its x
            // and y are zero, and the split's own view below is this, moved by whole steps.
            float worldAxes[16];
            bx::mtxLookAt(worldAxes, bx::Vec3(sunDir[0], sunDir[1], sunDir[2]),
                          bx::Vec3(0.0f, 0.0f, 0.0f), up, bx::Handedness::Right);

            // How wide the split is and where it sits off the camera's target.
            float side = lighting.shadowRange;
            bx::Vec3 offset(0.0f, 0.0f, 0.0f);
            if (lighting.shadowFitBelow > 0.0f) fitSplit(camera, lighting, worldAxes, &side, &offset);

            const bx::Vec3 focus(camera.target[0] + splitSlide_[0] + offset.x,
                                 camera.target[1] + splitSlide_[1] + offset.y,
                                 camera.target[2] + splitSlide_[2] + offset.z);
            const float half = side * 0.5f;
            splitSide_ = side;
            // Far enough back that nothing casting into the frame is clipped out of it. This
            // is a reach along the sun and has nothing to do with the split's width.
            const float back = lighting.shadowRange;
            const bx::Vec3 eye(focus.x + sunDir[0] * back, focus.y + sunDir[1] * back,
                               focus.z + sunDir[2] * back);

            // Snapping: the sun's axes through the origin, moved by a whole number of steps
            // -- x and y in texels, z in the D16 map's own quantum -- to where the eye rounds
            // down to. Built from the whole numbers and not as "the eye plus its remainder",
            // because that sum rounds differently every frame: frames whose split had not
            // moved still got a matrix a few bits apart, and a blocker-search tap sitting on
            // a texel edge flipped between two texels -- a pixel jumping between lit and
            // shadowed while nothing moved. Now a split that has not stepped is the same bits.
            //
            // It snapped the focus in its own light space once, where the focus is always at
            // the origin: the floor had nothing to remove, the split crawled with the camera,
            // and float noise either side of zero flipped it a whole texel now and then. Depth
            // was never snapped, so every stored depth re-rounded each frame: flickering acne
            // on every wall facing the sun. tools/shimmer.py measured all three;
            // docs/shadow-probe.md.
            const float texel = side / float(shadowSize_);
            const float quantum = back * 2.0f / 65535.0f;
            const bx::Vec3 e = bx::mul(eye, worldAxes);
            const float stepsX = std::floor(e.x / texel);
            const float stepsY = std::floor(e.y / texel);
            const float stepsZ = std::floor(e.z / quantum);
            float snap[16];
            bx::mtxTranslate(snap, -stepsX * texel, -stepsY * texel, -stepsZ * quantum);
            float snappedView[16];
            bx::mtxMul(snappedView, worldAxes, snap);

            // The probe: the split's centre and eye read back out of the matrix that is
            // actually drawn with -- not out of the arithmetic above, which is what it tests
            // -- and put on the world's grid.
            {
                float unsnap[16];
                bx::mtxInverse(unsnap, snappedView);
                const bx::Vec3 centre =
                    bx::mul(bx::Vec3(0.0f, 0.0f, bx::mul(focus, snappedView).z), unsnap);
                const bx::Vec3 c = bx::mul(centre, worldAxes);
                const bx::Vec3 drawnEye =
                    bx::mul(bx::mul(bx::Vec3(0.0f, 0.0f, 0.0f), unsnap), worldAxes);
                split_.texel = texel;
                split_.texelX = c.x / texel;
                split_.texelY = c.y / texel;
                split_.depthQuanta = drawnEye.z / quantum;
            }

            float lightProj[16];
            // The depth range is cut to what the split can hold. MU4 measured the cost of
            // not doing it: at 79 m the D16 steps were coarse enough that the bias lifted
            // the shadow off the figure's feet.
            bx::mtxOrtho(lightProj, -half, half, -half, half, 0.0f, back * 2.0f, 0.0f,
                         homogeneous, bx::Handedness::Right);

            float lightViewProj[16];
            bx::mtxMul(lightViewProj, snappedView, lightProj);

            bgfx::setViewFrameBuffer(ViewShadow, shadowFb_);
            bgfx::setViewRect(ViewShadow, 0, 0, shadowSize_, shadowSize_);
            bgfx::setViewClear(ViewShadow, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewShadow, snappedView, lightProj);

            // No front-face cull here, whatever a previous comment claimed: every material
            // in MU2's build is double sided, so there is nothing to cull and the bias has
            // to carry the whole job on its own.
            const uint64_t depthState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
            bgfx::setUniform(uMaterial_, noCutout);
            if (ground) submitGround(ViewShadow, groundShadowProgram_, *ground, depthState, false);
            if (!shadowBatches.empty()) {
                submitBatches(ViewShadow, shadowProgram_, skinnedShadowProgram_, shadowBatches,
                              idb, depthState, false);
            }
            // A fading figure still casts, dithered by fs_shadow. It is in the casters' own
            // list already when the camera culls separately; when it does not, that list IS
            // the camera's, which is the one it was kept out of.
            if (!separateCasters && !fadeBatches.empty()) {
                submitBatches(ViewShadow, shadowProgram_, skinnedShadowProgram_, fadeBatches, idb,
                              depthState, false);
            }

            // --- view 1: the prepass -------------------------------------------------
            bgfx::setViewFrameBuffer(ViewPrepass, prepassFb_);
            bgfx::setViewRect(ViewPrepass, 0, 0, uint16_t(width_), uint16_t(height_));
            bgfx::setViewClear(ViewPrepass, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000,
                               1.0f, 0);
            bgfx::setViewTransform(ViewPrepass, view, proj);
            const uint64_t prepassState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                          BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
            bgfx::setUniform(uMaterial_, noCutout);
            if (ground) submitGround(ViewPrepass, groundPrepassProgram_, *ground, prepassState, false);
            if (total > 0) {
                submitBatches(ViewPrepass, prepassProgram_, skinnedPrepassProgram_, batches_, idb,
                              prepassState, false);
            }

            // --- view 4's shared uniforms -------------------------------------------
            const float sunDirUniform[4] = {sunDir[0], sunDir[1], sunDir[2], lighting.sunStrength};
            const float sunColour[4] = {lighting.sunColour[0], lighting.sunColour[1],
                                        lighting.sunColour[2], lighting.ambientStrength};
            const float skyColour[4] = {lighting.skyColour[0], lighting.skyColour[1],
                                        lighting.skyColour[2], lighting.horizonPaleness};
            const float groundColour[4] = {lighting.groundColour[0], lighting.groundColour[1],
                                           lighting.groundColour[2], 0.0f};
            const float dust[4] = {lighting.dustColour[0], lighting.dustColour[1],
                                   lighting.dustColour[2], lighting.dustDensity};
            const float camPos[4] = {camera.position[0], camera.position[1], camera.position[2],
                                     camera.farPlane};
            const uint16_t hw = uint16_t(std::max(1, width_ / 2));
            const uint16_t hh = uint16_t(std::max(1, height_ / 2));
            // Pixels per unit at unit depth, in the SSAO target's own pixels: half its height
            // over the tangent of the half field of view. This is what turns a radius in
            // metres into a radius on screen, and without it the pass sampled itself.
            const float projScale =
                0.5f * float(hh) /
                std::tan(camera.fovDegrees * 0.5f * 3.14159265f / 180.0f);
            const float params[4] = {lighting.ssaoRadius, lighting.ssaoStrength,
                                     lighting.exposure, projScale};

            // --- views 2 and 3: SSAO and its blur, at half resolution ----------------


            bgfx::setViewFrameBuffer(ViewSsao, ssaoFb_);
            bgfx::setViewRect(ViewSsao, 0, 0, hw, hh);
            bgfx::setViewClear(ViewSsao, 0, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewSsao, nullptr, nullptr);
            bgfx::setUniform(uParams_, params);
            const float tanHalfY = std::tan(camera.fovDegrees * 0.5f * 3.14159265f / 180.0f);
            const float camRay[4] = {tanHalfY * float(width_) / float(height_), tanHalfY, 0.0f,
                                     0.0f};
            bgfx::setUniform(uCamRay_, camRay);
            const float prepassSize[4] = {float(width_), float(height_), 1.0f / float(width_),
                                          1.0f / float(height_)};
            bgfx::setUniform(uPrepassSize_, prepassSize);
            bgfx::setTexture(6, sPrepass_, prepassColour_);
            screenPass(ViewSsao, msaa_ > 1 ? ssaoMsProgram_ : ssaoProgram_);

            bgfx::setViewFrameBuffer(ViewBlur, blurFb_);
            bgfx::setViewRect(ViewBlur, 0, 0, hw, hh);
            bgfx::setViewClear(ViewBlur, 0, 0, 1.0f, 0);
            bgfx::setViewTransform(ViewBlur, nullptr, nullptr);
            bgfx::setUniform(uParams_, params);
            bgfx::setUniform(uPrepassSize_, prepassSize);
            bgfx::setTexture(6, sPrepass_, prepassColour_);
            bgfx::setTexture(7, sAo_, ssaoTex_);
            screenPass(ViewBlur, msaa_ > 1 ? blurMsProgram_ : blurProgram_);

            // --- view 4: the one lit pass --------------------------------------------
            float shadowMtx[16];
            // NDC to texture space: x and y into [0,1] with y flipped, since Metal's origin
            // is the top left. z is already [0,1] where homogeneousDepth is false.
            float bias[16];
            bx::mtxIdentity(bias);
            bias[0] = 0.5f;
            bias[5] = -0.5f;
            bias[12] = 0.5f;
            bias[13] = 0.5f;
            if (homogeneous) {
                bias[10] = 0.5f;
                bias[14] = 0.5f;
            }
            bx::mtxMul(shadowMtx, lightViewProj, bias);

            // The split's depth runs 0..1 over this many metres, which is what turns a
            // depth difference back into a distance.
            const float depthRange = back * 2.0f;
            // The sun is a directional light and its split is orthographic, so the penumbra
            // is the blocker's gap times the tangent of its half angle -- a distance, turned
            // into uv by the split's width. This scale is only half the sum: the shader has
            // to multiply by it and *not* divide by the blocker's depth, which is the
            // point-light formula. That divide survived the first review here while a
            // comment on this line claimed it had gone; it was still in fs_shade.sc, still
            // widening the penumbra by about 2.1x at the bench's blocker depth, and it came
            // out on the second review. A comment is not a fix.
            const float tanHalfAngle =
                std::tan(lighting.sunAngleDegrees * 0.5f * 3.14159265f / 180.0f);
            const float penumbraScale = tanHalfAngle * depthRange / side;
            // The bias is a distance too. Held in the sheet in metres and turned into the
            // split's own 0..1 depth here, because 0.0015 of an NDC over a 120 m range is
            // 18 cm of peter-panning on a map whose D16 quantum is under 2 mm.
            const float depthBias = lighting.shadowBiasMetres / depthRange;
            const float shadowParams[4] = {depthBias, penumbraScale, 1.0f / float(shadowSize_),
                                           lighting.shadowNormalBias};

            bgfx::setViewFrameBuffer(ViewShade, shadeFb_);
            bgfx::setViewRect(ViewShade, 0, 0, uint16_t(width_), uint16_t(height_));
            // The depth is the prepass's and is kept; only the colour is cleared.
            bgfx::setViewClear(ViewShade, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
            bgfx::setViewTransform(ViewShade, view, proj);

            // Kept, and set on every shade draw by bindShadeInputs rather than once here.
            std::memcpy(shade_.sunDir, sunDirUniform, sizeof(shade_.sunDir));
            std::memcpy(shade_.sunColour, sunColour, sizeof(shade_.sunColour));
            std::memcpy(shade_.skyColour, skyColour, sizeof(shade_.skyColour));
            std::memcpy(shade_.groundColour, groundColour, sizeof(shade_.groundColour));
            std::memcpy(shade_.dust, dust, sizeof(shade_.dust));
            std::memcpy(shade_.camPos, camPos, sizeof(shade_.camPos));
            std::memcpy(shade_.params, params, sizeof(shade_.params));
            std::memcpy(shade_.shadowMtx, shadowMtx, sizeof(shade_.shadowMtx));
            std::memcpy(shade_.shadowParams, shadowParams, sizeof(shade_.shadowParams));
            // The map's corner on the world's texel grid, which the world-anchored disc turn
            // reads. The centre is a whole number of texels by the snap; the round only drops
            // the float noise the read-back carries.
            shade_.shadowDebug[0] = shadowDebug_[0];
            shade_.shadowDebug[1] = shadowDebug_[1];
            shade_.shadowDebug[2] = std::round(split_.texelX) - float(shadowSize_) * 0.5f;
            shade_.shadowDebug[3] = std::round(split_.texelY) + float(shadowSize_) * 0.5f;
            // The blocker search and the widest penumbra, in metres and then in this split's
            // uv. They were 6 and 24 texels, which on the 60 m, 2048 split the look was judged
            // on is 17.6 cm and 70 cm; counted in texels, a finer map drew a smaller shadow --
            // the search shrank to 5 cm at 4096, the outer penumbra was cut off, and a finer
            // map looked cheaper because fewer pixels found a blocker at all.
            constexpr float kSearchMetres = 0.176f;
            constexpr float kWidestPenumbraMetres = 0.703f;
            shade_.shadowReach[0] = kSearchMetres / side;
            shade_.shadowReach[1] = kWidestPenumbraMetres / side;
            shade_.shadowReach[2] = shade_.shadowReach[3] = 0.0f;
            bgfx::setTexture(4, sShadowCompare_, shadowMap_,
                             BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP |
                                 BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(5, sShadowDepth_, shadowMap_,
                             BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(7, sAo_, blurTex_);

            // Depth EQUAL against what the prepass laid down, and no depth write: nothing
            // here is shaded twice.
            const uint64_t shadeState =
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL;
            if (ground) submitGround(ViewShade, groundShadeProgram_, *ground, shadeState, true);
            if (total > 0) {
                submitBatches(ViewShade, shadeProgram_, skinnedShadeProgram_, batches_, idb,
                              shadeState, true);
            }

            // --- view 5, before the glow: whatever is only part there ------------------
            // Two passes over the same instances, and the order is the whole technique:
            //
            //   1. its DEPTH alone, tested and written against the world's, so the nearest
            //      surface of the figure wins;
            //   2. the same shade the rest of the frame got, tested EQUAL against that depth
            //      and blended by the alpha fs_shade now writes -- which is the fade.
            //
            // Without the first pass, a figure at half opacity shows its own back through its
            // chest and the blend doubles wherever it overlaps itself. It is here rather than
            // in the shade pass because it must be blended over a finished picture, and before
            // the glow and the sprites because it is solid scene and they are what is added
            // over it. The view is sequential, so submission order is draw order.
            if (!fadeBatches.empty() && bgfx::isValid(prepassProgram_)) {
                bgfx::setViewMode(ViewTransparent, bgfx::ViewMode::Sequential);
                const uint64_t depthOnly = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
                submitBatches(ViewTransparent, prepassProgram_, skinnedPrepassProgram_,
                              fadeBatches, idb, depthOnly, false);
                const uint64_t blended = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_EQUAL |
                                         BGFX_STATE_BLEND_ALPHA;
                submitBatches(ViewTransparent, shadeProgram_, skinnedShadeProgram_, fadeBatches,
                              idb, blended, true);
            }

            // --- view 5, first half: MU's BlendMeshes --------------------------------
            // The glow parts the three passes above skipped, added into the shade target
            // against the prepass's depth and writing none. Here and not in Effects because
            // they are meshes that ride the frame's instance buffer, which lives in this
            // block; the view's target and transform are set below with the sprites'.
            if (total > 0 && bgfx::isValid(glowProgram_) && bgfx::isValid(skinnedGlowProgram_)) {
                const uint64_t glowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
                                           BGFX_STATE_BLEND_ADD;
                submitBatches(ViewTransparent, glowProgram_, skinnedGlowProgram_, batches_, idb,
                              glowState, false, true);
            }

            // --- views 17 to 59: the reflection probe, for the next frame's shade --------
            // From the casters' list, which is the wider one: the camera's chunks leave out
            // what is behind it, and behind the camera is half of what a cube sees.
            if (probeOk_ && probeOn_) drawProbe(camera, ground, shadowBatches, idb);
        }
    }

    if (!anything) {
        // Nothing was drawn, so nothing cleared the shade target and the present below would
        // hand the screen a texture that has never been written.
        bgfx::setViewFrameBuffer(ViewShade, shadeFb_);
        bgfx::setViewRect(ViewShade, 0, 0, uint16_t(width_), uint16_t(height_));
        bgfx::setViewClear(ViewShade, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
        bgfx::touch(ViewShade);
    }

    // --- view 5: the transparent pass -------------------------------------------------
    // Into the SAME target the shade pass wrote, which is the whole reason this view is here
    // and not after the present: the target is HDR and multisampled, so an additive flame
    // adds to a linear radiance, gets antialiased edges from the MSAA already being paid
    // for, and is tonemapped with the scene by the present below. Drawn after the resolve it
    // would be LDR sprites over an already-tonemapped image.
    //
    // It shares the prepass's depth as an attachment, so it can test against the world
    // without writing to it. The sort inside Effects::draw is what decides the picture.
    bgfx::setViewFrameBuffer(ViewTransparent, shadeFb_);
    bgfx::setViewRect(ViewTransparent, 0, 0, uint16_t(width_), uint16_t(height_));
    // No clear at all: the shade pass's colour and the prepass's depth are both wanted.
    bgfx::setViewClear(ViewTransparent, 0, 0, 1.0f, 0);
    effects_.setFlameStrength(lighting.flameStrength);
    effects_.draw(ViewTransparent, view, proj, camera.position);
    drawCount_ += effects_.lastDrawCount();

    bloom(lighting);

    // --- the present ------------------------------------------------------------------
    const float params[4] = {lighting.ssaoRadius, lighting.ssaoStrength, lighting.exposure, 0.0f};
    const bool bloomed = bgfx::isValid(bloomDownProgram_) && bgfx::isValid(bloomUpProgram_);
    const float bloomParams[4] = {0.0f, 0.0f, bloomed ? lighting.bloomStrength : 0.0f, 0.0f};
    bgfx::setUniform(uBloom_, bloomParams);
    const float present[4] = {lighting.sharpen, lighting.contrast, 1.0f / float(width_),
                              1.0f / float(height_)};
    bgfx::setUniform(uPresent_, present);
    const float grade[4] = {lighting.tonemap, lighting.saturation * (1.0f - drain_),
                            lighting.split, 0.0f};
    const float tintLow[4] = {lighting.tintLow[0], lighting.tintLow[1], lighting.tintLow[2], 0.0f};
    const float tintHigh[4] = {lighting.tintHigh[0], lighting.tintHigh[1], lighting.tintHigh[2],
                               0.0f};
    bgfx::setUniform(uGrade_, grade);
    bgfx::setUniform(uTintLow_, tintLow);
    bgfx::setUniform(uTintHigh_, tintHigh);
    bgfx::setTexture(9, sBloom_, bloomTex_[0]);
    bgfx::setViewFrameBuffer(ViewPresent, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewPresent, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::setViewClear(ViewPresent, BGFX_CLEAR_COLOR, 0x101418ff, 1.0f, 0);
    bgfx::setViewTransform(ViewPresent, nullptr, nullptr);
    bgfx::setUniform(uParams_, params);
    bgfx::setTexture(8, sColour_, shadeColour_);
    screenPass(ViewPresent, presentProgram_);

    // View 7 is the HUD's, and is submitted empty until sprint 7 fills it. A view bgfx sees
    // nothing in is dropped, and an account with no rows reads as free rather than unbuilt.
    bgfx::setViewFrameBuffer(ViewHud, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewHud, 0, 0, uint16_t(width_), uint16_t(height_));
    bgfx::touch(ViewHud);
}

}  // namespace mu::gfx
