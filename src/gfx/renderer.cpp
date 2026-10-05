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

// MuMain's two clocks off WorldTime, which is milliseconds: g_Luminosity (SceneManager.cpp:1283)
// and the chrome's wave (ZzzBMD.cpp:1314). Missing sheets add nothing: the strength goes to 0
// and a texture that exists stands in, so no stage is left unbound. Only for the mesh draws:
// stages 9 and 10 are the land's second layer in fs_ground.
void Renderer::bindShine(bool stage) {
    const bool sheets = bgfx::isValid(shineChrome_) && bgfx::isValid(shineShiny_);
    const float strength = stage ? shineStageStrength_ : shineStrength_;
    const float refine[4] = {std::sin(elapsed_ * 4.0f) * 0.15f + 0.6f,
                             std::fmod(elapsed_, 10.0f) * 0.1f, sheets ? strength : 0.0f,
                             shineTint_};
    bgfx::setUniform(uRefine_, refine);
    // The excellent pass's L: WorldTime is milliseconds, so sin(WorldTime * 0.002) is two
    // radians a second.
    const bool excellent = bgfx::isValid(shineChrome2_);
    const float star[4] = {shineStar_, stage ? 0.0f : shineGlow_,
                           std::sin(elapsed_ * 2.0f) * 0.5f + 0.5f,
                           excellent ? shineExcellent_ : 0.0f};
    bgfx::setUniform(uRefineStar_, star);
    bgfx::setTexture(9, sChrome_, sheets ? shineChrome_ : whiteAo_, 0);
    bgfx::setTexture(10, sShiny_, sheets ? shineShiny_ : whiteAo_,
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(11, sChrome2_, excellent ? shineChrome2_ : whiteAo_, 0);
}

void Renderer::bindShadeInputs() {
    bgfx::setUniform(uSunDir_, shade_.sunDir);
    bgfx::setUniform(uSunColour_, shade_.sunColour);
    bgfx::setUniform(uSkyColour_, shade_.skyColour);
    bgfx::setUniform(uGroundColour_, shade_.groundColour);
    // A probe face sees the town from inside the air too, and its reflection is of the
    // town as the eye sees it; but the probe stands a few metres from what it holds, so
    // the dust it draws is the near air's, which is little.
    bgfx::setUniform(uDust_, shade_.dust);
    bgfx::setUniform(uEdge_, edge_);
    // Stage 6 for fs_shade, which reads no prepass; the land rebinds it to 15 after this.
    bgfx::setUniform(uAbyss_, abyssParams_);
    bgfx::setTexture(6, sAbyss_, bgfx::isValid(abyss_) ? abyss_ : whiteAo_,
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
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
    bgfx::setTexture(7, sAo_, probePass_ || !aoOn_ ? whiteAo_ : blurTex_);
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
        bgfx::setUniform(uTransientTo_, transientTo_, kMaxTransientLights);
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

void Renderer::packInstance(const Drawable& d, float out[kInstanceFloats]) {
    std::memcpy(out, d.transform, sizeof(float) * 16);
    std::memcpy(out + 16, d.light, sizeof(float) * 4);
    // No row of its own means the bind row, which is row 0 and is the identity. -1 would be
    // read as a texel outside the palette. z is the item's plus, for the shine.
    out[20] = d.sway ? -1.0f : float(d.paletteRow < 0 ? kBindRow : d.paletteRow);
    out[21] = d.fade;
    out[22] = float(d.refine);
    out[23] = packRefineColour(d.refineColour);
}

const bgfx::VertexLayout& Renderer::instanceLayout() {
    static const bgfx::VertexLayout layout = [] {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
            .end();
        return l;
    }();
    return layout;
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
            if (int(part.material) == batch.hiddenMaterial) continue;
            const content::Material& material = mesh.materials()[part.material];
            // Soft alpha (content::Material::softAlpha): out of the opaque prepass and shade,
            // and alone in its own blended pass after them. Every other view takes it as the
            // plain cut-out it also is.
            if (softSelect_ == SoftSelect::Skip && material.softAlpha) continue;
            if (softSelect_ == SoftSelect::Only && !material.softAlpha) continue;
            // A glow is drawn in its own pass and in no other. See the header. Except that a
            // glow that casts (Material::glowShadow) is in the sun's split as well, and in the
            // hover ring's mask, solid: the Ice Monster's body is that glow, and without it the
            // mask held only its few solid parts and the ring cut across its body.
            const bool casting = material.glowShadow > 0.0f && view == ViewShadow;
            const bool ringView = view >= ViewOutlineMask && view < ViewOutlineMask + kOutlineRings;
            const bool ringed = material.glowShadow > 0.0f && ringView;
            if (material.glow != glowPass && !casting && !ringed) continue;
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
            float scrollOffset = -std::fmod(elapsed_ * material.scrollPerSecond, 1.0f);
            // MU's water frames: the frame, 0 to 31, rather than a slide (content::Material).
            if (material.waterFrames) {
                scrollOffset = std::floor(std::fmod(elapsed_ * material.scrollPerSecond, 32.0f));
            }
            // An item's glow as ItemObjectAttribute sets it: BlendMeshLight's breathing,
            // sin(WorldTime*0.004)*a + b with WorldTime in ms, and a random jump of its sheet in
            // steps of `jitter` (MU's (rand()%10)*0.1 each frame it draws). The jump is drawn 25
            // times a second, MU's own frame rate, keyed by the clock and the material, since
            // at this renderer's rate a jump every frame is a strobe rather than a shimmer.
            float glowLevel = material.itemGlow ? 1.0f : glowStrength_;
            if (glowPass && material.pulse[1] < 0.0f) {
                // A negative base is MoveCharacterVisual's flicker, the Gorgon's
                // `BlendMeshLight = (rand()%10)*0.1` rolled each frame (ZzzCharacter.cpp:6062):
                // one of 0.0 to 0.9, times -base, rolled 25 times a second as the jitter is.
                uint32_t h = uint32_t(elapsed_ * 25.0f) * 2246822519u ^
                             uint32_t(reinterpret_cast<uintptr_t>(&material));
                h ^= h >> 15;
                h *= 2654435761u;
                h ^= h >> 13;
                glowLevel *= float(h % 10u) * 0.1f * -material.pulse[1];
            } else if (glowPass) {
                glowLevel *= std::sin(elapsed_ * (material.slowPulse ? 1.0f : 4.0f)) *
                                 material.pulse[0] +
                             material.pulse[1];
                if (material.jitter > 0.0f) {
                    uint32_t h = uint32_t(elapsed_ * 25.0f) * 2654435761u ^
                                 uint32_t(reinterpret_cast<uintptr_t>(&material));
                    h ^= h >> 15;
                    h *= 2246822519u;
                    h ^= h >> 13;
                    scrollOffset += float(h % 10u) * material.jitter;
                }
            }
            // In the glow pass y is how the scroll runs instead, which fs_glow alone reads: 1
            // along U, 2 with the sheet's alpha held still (content::Material::maskHeld).
            const float materialParams[4] = {material.cutout,
                                             glowPass ? (material.scrollAlongU ? 1.0f : 0.0f) +
                                                            (material.maskHeld ? 2.0f : 0.0f) +
                                                            (material.waterFrames ? 4.0f : 0.0f)
                                             : (material.twoSided ? 1.0f : 0.0f) +
                                                 (material.calibrated ? 2.0f : 0.0f) +
                                                 // Not in the hover ring's mask, which
                                                 // draws with fs_shadow: there a soft part's
                                                 // dither left holes the ring traced as
                                                 // dots (the user, 2026-10-04: 'tiiny yellow
                                                 // dots ono hover outline'). The plain cut.
                                                 (material.softAlpha && !ringView ? 4.0f : 0.0f),
                                             glowPass ? glowLevel
                                             // In a depth-only pass z is how much of the
                                             // shadow is thinned away by fs_shadow's dither:
                                             // 0, but a casting glow's 1 - strength.
                                             : !bindMaterial
                                                 ? (casting ? 1.0f - material.glowShadow : 0.0f)
                                                 : material.roughnessFactor,
                                             glowPass ? scrollOffset : material.metalFactor};
            bgfx::setUniform(uMaterial_, materialParams);
            // The water's sway (common.sh's swayed), for the still plants marked to take it.
            const float swayParams[4] = {elapsed_, sway_, 0.0f, 0.0f};
            bgfx::setUniform(uSway_, swayParams);
            // The albedo is bound even in the depth passes, because the cutout reads its alpha.
            const bool waterSheet =
                glowPass && material.waterFrames && bgfx::isValid(causticSheet_);
            bgfx::setTexture(0, sAlbedo_, waterSheet ? causticSheet_ : material.albedo,
                             glowPass && material.glowClamped()
                                 ? BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
                                 : UINT32_MAX);
            if (bindMaterial) {
                bgfx::setTexture(1, sNormal_, material.normal);
                bgfx::setTexture(2, sOrm_, material.orm);
                bgfx::setTexture(3, sEmissive_, material.emissive);
                const float translucency[4] = {material.translucency, 0.0f, 0.0f, 0.0f};
                bgfx::setUniform(uTranslucency_, translucency);
                bindShadeInputs();
                // Here and not in bindShadeInputs: the land binds its second layer on stages 9
                // to 11 and then calls that, and the chrome went down over the town's ground.
                bindShine();
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
            if (bgfx::isValid(batch.resident)) {
                bgfx::setInstanceDataBuffer(batch.resident, batch.first, batch.count);
            } else {
                bgfx::setInstanceDataBuffer(&idb, batch.first, batch.count);
            }
            bgfx::setState(drawState);
            bgfx::submit(view, batchProgram);
            ++drawCount_;
        }
    }
}

void Renderer::bloom(const Lighting& lighting) {
    if (!bgfx::isValid(bloomDownProgram_) || !bgfx::isValid(bloomUpProgram_)) return;
    // Off in Options is a strength of 0, and nine passes that add nothing are not drawn.
    if (lighting.bloomStrength <= 0.0f) return;
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

void Renderer::submitGrass(bgfx::ViewId view, bgfx::ProgramHandle program,
                           const GrassField& grass, uint64_t state) {
    if (!bgfx::isValid(program)) return;

    // One draw: a run of the instance buffer, a sheet, and the numbers that sheet is read with.
    auto draw = [&](const GrassField::Batch& batch, const float* card, const float* vary,
                    const float* sheet, float density, uint32_t first, uint32_t indices,
                    float colour) {
        if (batch.count == 0 || !bgfx::isValid(batch.sheet)) return;
        // The sun, the sky, the shadow map, the AO and the lamps, which fs_grass reads as the
        // land does. The field used to inherit them from the ground's draws, submitted before
        // it; drawn first now (Renderer::draw), it has to bind its own.
        bindShadeInputs();
        bgfx::setUniform(uGrassCard_, card);
        bgfx::setUniform(uGrassWind_, grass.wind);
        bgfx::setUniform(uGrassRoot_, grass.root);
        bgfx::setUniform(uGrassTip_, grass.tip);
        bgfx::setUniform(uGrassVary_, vary);
        bgfx::setUniform(uGrassThrough_, grass.through);
        bgfx::setUniform(uGrassShape_, grass.shape);
        bgfx::setUniform(uGrassSheet_, sheet);
        bgfx::setUniform(uGrassReach_, grass.reach);
        bgfx::setUniform(uGrassWalkers_, grass.walkers, GrassField::kMaxWalkers);
        // Per batch, because it is per SHEET: Lorencia's two are 256x64, Noria's third is
        // 256x128 and the meadow's is 512x128, and a mip level is worked out per axis.
        const float size[4] = {batch.width, batch.height, density, colour};
        bgfx::setUniform(uGrassSize_, size);
        bgfx::setUniform(uGrassStorm_, grass.storm);
        // Clamped, and it matters: a card's uv runs across ONE column of its sheet, and a
        bgfx::setUniform(uGrassWake_, grass.wake);
        // Only when there is a wake to read. This runs on every grass draw, and a measuring
        // run -- where nobody is played and the radius is 0 -- should not pay 24 vec4s of
        // upload for an array the shader's bound is about to skip.
        if (grass.wake[2] > 0.0f) {
            bgfx::setUniform(uGrassSteps_, grass.steps, GrassField::kMaxSteps);
        }
        // wrapped sampler bleeds the column beside it in along the cut. Not point-sampled --
        // the painted strokes want the filter -- so the bleed would be half a texel of the
        // wrong tuft down every edge of every card in the field.
        bgfx::setTexture(0, sAlbedo_, batch.sheet, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bgfx::setVertexBuffer(0, grass.vertices);
        if (indices > 0) {
            bgfx::setIndexBuffer(grass.indices, first, indices);
        } else {
            bgfx::setIndexBuffer(grass.indices);
        }
        bgfx::setInstanceDataBuffer(&grass.instances, batch.first, batch.count);
        // Both faces, which is what the state carries no BGFX_STATE_CULL_* for. A card is a
        // surface with no inside, and half a scattered field is turned away at any moment;
        // fs_grass turns the normal to face whoever is looking. Culling would leave holes that
        // move with the wind.
        bgfx::setState(state);
        bgfx::submit(view, program);
        ++drawCount_;
    };

    for (int i = 0; i < grass.batchCount; ++i) {
        draw(grass.batches[i], grass.card, grass.vary, grass.sheet, 1.0f, grass.swardFirst,
             grass.swardIndices, grass.colour);
    }
    // And the flowers over the top of it, one more draw across the same patches. The grade
    // goes with it: fs_grass grades a plant's leaves and stems to the lawn's green and leaves
    // its petals as painted.
    draw(grass.meadow, grass.meadowCard, grass.meadowVary, grass.meadowSheet,
         grass.meadowDensity, 0, grass.meadowIndices, grass.colour);
}

void Renderer::submitGround(bgfx::ViewId view, bgfx::ProgramHandle program,
                            const content::Ground& g, uint64_t state, bool lit) {
    for (const content::GroundPart& part : g.parts()) {
        if (!g.draws(part)) continue;
        if (lit) {
            // First: it binds stages 6 and 15, and the land wants its own on both.
            bindShadeInputs();
            const content::GroundLayer* l = part.layers;
            // The bite is MU2's own 0.35, in w.
            const float repeat[4] = {l[0].repeat, l[1].repeat, l[2].repeat, 0.35f};
            bgfx::setUniform(uGroundRepeat_, repeat);
            // w: which layers are water, one bit each, for the lead fs_ground gives it.
            const float relief[4] = {l[0].relief, l[1].relief, l[2].relief,
                                     float((l[0].water ? 1 : 0) | (l[1].water ? 2 : 0) |
                                           (l[2].water ? 4 : 0))};
            bgfx::setUniform(uGroundRelief_, relief);
            bgfx::setUniform(uWaterGlow_, waterGlow_);
            bgfx::setUniform(uGroundWet_, groundWet_);
            // xyz are each layer's water slide, in widths of its own sheet: MuMain's
            // WaterMove, `(WorldTime % 20000) * 0.00005` (ZzzLodTerrain.cpp), added to U on
            // every tile that wears TileWater01 -- one sheet width every twenty seconds, along
            // the columns, the same on every water tile so the river moves as one. w is how
            // many layers the part weighs: most of the land is one, and reads one set.
            // Times the sheet's water_flow: Noria's water is puddles, and a puddle sliding a
            // sheet every twenty seconds reads as a river in a hole (the user, 2026-09-28).
            //
            // Unless the world names its rivers (content::buildFlow): then the water runs
            // along each channel instead, and x is where the cross-fade's cycle is, y the
            // texel row of the weight map's flow band, z how far a copy is dragged in tiles.
            // y is -1 without one, and z is MU's slide.
            //
            // The slide wraps after 500 sheets, not MU's one: water_variety's turned copy
            // (fs_ground `turned`) sees a slide of s as s * 0.57 * (0.8, 0.6) of its own sheet,
            // whole only at 500 (228, 171), so a wrap at one jumped it by (0.456, 0.342) and the
            // Lost Tower's whole lava shuddered every hundred seconds (the user, 2026-10-02).
            const float row = g.flowRow();
            const float slide = std::fmod(elapsed_ * waterFlow_, 20.0f * 500.0f) * 0.05f;
            const float cycle = std::fmod(elapsed_ * waterFlow_ / content::Ground::kFlowCycle, 1.0f);
            const float blend[4] = {cycle, row, row >= 0.0f ? content::Ground::kFlowReach : slide,
                                    float(part.layerCount)};
            bgfx::setUniform(uGroundBlend_, blend);
            bgfx::setTexture(0, sAlbedo_, l[0].albedo);
            bgfx::setTexture(1, sNormal_, l[0].normal);
            bgfx::setTexture(2, sOrm_, l[0].orm);
            // MU's caustics (content::Ground::setCaustic): the layer laid on TileWater01's
            // slot is drawn as the 32 frames added, not as a sheet blended in, so its albedo
            // stage takes the frames' sheet. x the frame, one a reference frame (25 a second,
            // WaterTextureNumber, SceneManager.cpp:326-337); y how bright, the sheet's caustic;
            // z which layers, a bit each. Never layer 0: MU only ever overlays it.
            int causticBits = 0;
            if (caustic_ > 0.0f && bgfx::isValid(g.caustic())) {
                for (int k = 1; k < part.layerCount; ++k) {
                    if (part.slots[k] >= 0 && part.slots[k] == g.causticSlot()) causticBits |= 1 << k;
                }
            }
            const float causticParams[4] = {std::floor(std::fmod(elapsed_ * 25.0f, 32.0f)),
                                            caustic_, float(causticBits), 0.0f};
            bgfx::setUniform(uCaustic_, causticParams);
            bgfx::setTexture(9, sAlbedo2_, (causticBits & 2) ? g.caustic() : l[1].albedo);
            bgfx::setTexture(10, sNormal2_, l[1].normal);
            bgfx::setTexture(11, sOrm2_, l[1].orm);
            bgfx::setTexture(3, sAlbedo3_, (causticBits & 4) ? g.caustic() : l[2].albedo);
            bgfx::setTexture(8, sNormal3_, l[2].normal);
            bgfx::setTexture(12, sOrm3_, l[2].orm);
            // The shared corner weights, read through a B-spline; the vertex weights where
            // the land has none (a bench plot). content::Ground::splat.
            const bool splat = bgfx::isValid(g.weights()) && part.slots[0] >= 0;
            const float slots[4] = {float(part.slots[0]), float(part.slots[1]),
                                    float(part.slots[2]), splat ? 1.0f : 0.0f};
            bgfx::setUniform(uGroundSlots_, slots);
            const float* size = g.weightSize();
            const float weights[4] = {size[0], size[1], size[2],
                                      float(content::Ground::kWeightPad)};
            bgfx::setUniform(uGroundWeights_, weights);
            bgfx::setTexture(6, sGroundWeights_, splat ? g.weights() : l[0].albedo,
                             BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            // The abyss on 15, over the probe's cube: stage 6 is the weights here, and
            // bindShadeInputs (above, first, so nothing below is overwritten) put it on 6.
            bgfx::setTexture(15, sAbyss_, bgfx::isValid(abyss_) ? abyss_ : whiteAo_,
                             BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
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
    bx::mtxProj(proj, camera.fovDegrees, float(outWidth_) / float(outHeight_), camera.nearPlane,
                camera.farPlane, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
}

void Renderer::draw(const Camera& camera, const Lighting& lighting,
                    const std::vector<Drawable>& drawables, const content::Ground* ground,
                    const std::vector<Drawable>* casters, const GrassField* grass) {
    drawCount_ = 0;
    waterFlow_ = lighting.waterFlow;
    for (int i = 0; i < 3; ++i) waterGlow_[i] = lighting.waterGlow[i];
    waterGlow_[3] = lighting.waterVariety;
    groundWet_[0] = lighting.groundWet;
    groundWet_[1] = lighting.groundPuddles;
    caustic_ = lighting.caustic;
    sway_ = lighting.sway;
    causticSheet_ = ground ? ground->caustic() : bgfx::TextureHandle{bgfx::kInvalidHandle};
    // The chasms' dark, which is the world's and not the sheet's. content::Ground::abyss.
    abyss_ = ground ? ground->abyss() : bgfx::TextureHandle{bgfx::kInvalidHandle};
    if (bgfx::isValid(abyss_)) {
        std::memcpy(abyssParams_, ground->abyssParams(), sizeof(abyssParams_));
    } else {
        abyssParams_[1] = 0.0f;
    }
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

    // The lights' flicker, when it moved: the three rows of the 256-wide texture, 12 kB.
    if (lampsDirty_ && bgfx::isValid(lamps_)) {
        bgfx::updateTexture2D(lamps_, 0, 0, 0, 0, uint16_t(kMaxPointLights + 1), kLampRows,
                              bgfx::copy(lampCpu_.data(), uint32_t(lampCpu_.size() * sizeof(float))));
        lampsDirty_ = false;
    }
    lampParams_[0] = lighting.lampStrength;
    lampParams_[3] = lighting.lampShadow;
    glowStrength_ = lighting.glowStrength;
    probeOn_ = lighting.probe > 0.5f;
    aoOn_ = lighting.ssaoStrength > 0.0f;
    probeView_ = lighting.probeView;
    // Switched off, the cube is forgotten: switched back on, it starts from a whole new one
    // rather than reading one taken wherever the player stood when it went off.
    if (!probeOn_) {
        probeReady_ = probeFilterDue_ = false;
        probeNextFace_ = 0;
    }
    metalGain_ = lighting.metalGain;
    shineStrength_ = lighting.refineStrength;
    shineTint_ = lighting.refineTint;
    shineStageStrength_ = lighting.refineStageStrength;
    shineStar_ = lighting.refineStar;
    shineExcellent_ = lighting.excellentStrength;

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
    // Taken by this draw alone (setResidentCasters).
    const std::vector<ResidentBatch>* resident = residentCasters_;
    residentCasters_ = nullptr;
    const std::vector<Drawable>& casterList = casters ? *casters : drawables;
    const bool anything = !drawables.empty() || !casterList.empty() || ground != nullptr;
    if (anything) {
        // Grouped by mesh, keeping the order each mesh was first seen in, so a frame's draw
        // order does not shuffle between runs and a measurement stays comparable. The groups
        // live on the renderer and keep their capacity between frames; see Groups.
        groups_.clear();
        fadeGroups_.clear();
        casterGroups_.clear();
        fadeBatches_.clear();
        // Which drawables a list takes. The camera's list is split in two: what is only part
        // there is kept out of the opaque passes and drawn after them (almost always nothing
        // -- the character's entrance, a corpse going out). The sun's own list is taken WHOLE,
        // fading instances included: fs_shadow dithers each by its own fade. It used to take
        // the solid ones alone, which left a fading figure with no shadow at all whenever the
        // camera culled separately -- and in play it always does, so the dither in fs_shadow
        // was never reached and the character came in through the door casting nothing.
        enum class Take { Solid, Fading, All };
        auto group = [](const std::vector<Drawable>& list, Groups& out,
                        std::vector<Batch>& batches, Take take) {
            for (const Drawable& d : list) {
                if (!d.mesh) continue;
                const bool fading = d.fade < 1.0f;
                if (take == Take::Solid && fading) continue;
                if (take == Take::Fading && !fading) continue;
                const bool posed = d.paletteRow >= 0 || !d.inProbe;
                // A user-space pointer leaves its top byte clear, and the hidden material
                // (-1 to 254) rides there, so a bow with its arrow gone is a batch of its own.
                const uint64_t key = uint64_t(reinterpret_cast<uintptr_t>(d.mesh)) ^
                                     (uint64_t(uint8_t(d.hiddenMaterial + 1)) << 56);
                auto found = out.seen.find(key);
                if (found == out.seen.end()) {
                    out.seen.emplace(key, out.used);
                    out.add().push_back(&d);
                    batches.push_back(Batch{d.mesh, 0, 0, posed, d.hiddenMaterial});
                } else {
                    out.lists[found->second].push_back(&d);
                    if (posed) batches[found->second].posed = true;
                }
            }
        };
        group(drawables, groups_, batches_, Take::Solid);
        group(drawables, fadeGroups_, fadeBatches_, Take::Fading);
        const bool separateCasters = casters != nullptr;
        if (separateCasters) group(casterList, casterGroups_, casterBatches_, Take::All);

        // A 4x4 matrix, the instance's baked light, and the row its pose occupies in the
        // bone palette. The depth passes read the matrix and the row and skip the light,
        // which costs them nothing: the stride is what the buffer is walked by, not what
        // each shader reads.
        const uint32_t stride = 96;
        uint32_t total = groups_.count() + fadeGroups_.count() + casterGroups_.count();

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
            auto fill = [&](const Groups& from, std::vector<Batch>& batches) {
                for (size_t gi = 0; gi < from.used && total > 0; ++gi) {
                    batches[gi].first = written;
                    uint32_t count = 0;
                    for (const Drawable* d : from.lists[gi]) {
                        if (written >= total) break;
                        packInstance(*d, reinterpret_cast<float*>(idb.data + written * stride));
                        ++written;
                        ++count;
                    }
                    batches[gi].count = count;
                }
                batches.erase(std::remove_if(batches.begin(), batches.end(),
                                             [](const Batch& b) { return b.count == 0; }),
                              batches.end());
            };
            fill(groups_, batches_);
            fill(fadeGroups_, fadeBatches_);
            if (separateCasters) fill(casterGroups_, casterBatches_);
            // And the scenery that lives on the GPU, after the list's own casters.
            if (separateCasters && resident != nullptr) {
                for (const ResidentBatch& r : *resident) {
                    if (r.mesh == nullptr || r.count == 0 || !bgfx::isValid(r.buffer)) continue;
                    casterBatches_.push_back(Batch{r.mesh, r.first, r.count, r.posed, -1, r.buffer});
                }
            }
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
            // list already when the camera culls separately (group() takes that list whole);
            // when it does not, that list IS the camera's, which is the one it was kept out of.
            if (!separateCasters && !fadeBatches_.empty()) {
                submitBatches(ViewShadow, shadowProgram_, skinnedShadowProgram_, fadeBatches_,
                              idb, depthState, false);
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
                softSelect_ = SoftSelect::Skip;
                submitBatches(ViewPrepass, prepassProgram_, skinnedPrepassProgram_, batches_, idb,
                              prepassState, false);
                softSelect_ = SoftSelect::All;
            }

            // --- view 4's shared uniforms -------------------------------------------
            const float sunDirUniform[4] = {sunDir[0], sunDir[1], sunDir[2], lighting.sunStrength};
            const float sunColour[4] = {lighting.sunColour[0], lighting.sunColour[1],
                                        lighting.sunColour[2], lighting.ambientStrength};
            const float skyColour[4] = {lighting.skyColour[0], lighting.skyColour[1],
                                        lighting.skyColour[2], lighting.horizonPaleness};
            const float groundColour[4] = {lighting.groundColour[0], lighting.groundColour[1],
                                           lighting.groundColour[2], lighting.waterSheen};
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
            // Not drawn at all with ambient occlusion off in Options: the shade reads a white
            // texel instead (aoOn_), which is what an occlusion of nothing looks like.
            if (aoOn_) {
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
            }

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
            bgfx::setTexture(7, sAo_, aoOn_ ? blurTex_ : whiteAo_);

            // Depth EQUAL against what the prepass laid down, and no depth write: nothing
            // here is shaded twice.
            const uint64_t shadeState =
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL;
            // The ground AFTER the grass, which takes a view in submission order: bgfx sorts a
            // view by program, and the ground's program came first, so the turf under the field
            // was lit in full -- two or three material sets, the PCSS lookup, the lamp loop -- and
            // then covered. Drawn second, it fails EQUAL wherever the grass wrote a nearer depth
            // on all four samples, and those pixels showed grass in any case: the same picture
            // (docs/perf-audit-2k.md, A3).
            bgfx::setViewMode(ViewShade, bgfx::ViewMode::Sequential);
            // The grass, and it is the one thing in this view that does not test EQUAL.
            //
            // It is not in the prepass at all. It cannot be: a card is mostly empty, its edge
            // is antialiased into a sample COVERAGE by fs_grass, and the prepass writes the
            // view depth in the very alpha channel alpha-to-coverage would read. So the field
            // lays its own depth here -- tested LESS against what the prepass did write, so a
            // tuft behind a house is still hidden by the house, and written, so a tuft behind
            // another tuft is hidden by it.
            //
            // What that gives up is measured and written down: out of the prepass, the ground
            // under the field is shaded and then covered, where before the field's own prepass
            // depth made the ground fail EQUAL and skip its two blended material sets, its
            // PCSS lookup and its lamp loop. That saving was worth -0.26 ms -- the field was
            // FASTER than no field. It was spent on not shimmering. docs/grass.md. Drawing the
            // field before the ground (above) takes it back wherever a pixel is grass throughout.
            //
            // Alpha to coverage only where there are samples to cover: at --msaa 1 there is
            // one, and a fractional coverage on one sample is a dither. The hard cut is right
            // there, and fs_grass's ramp collapses to it on its own.
            const uint64_t grassState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                        BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                                        (msaa_ > 1 ? BGFX_STATE_BLEND_ALPHA_TO_COVERAGE : 0);
            if (grass) submitGrass(ViewShade, grassShadeProgram_, *grass, grassState);
            if (ground) submitGround(ViewShade, groundShadeProgram_, *ground, shadeState, true);
            if (total > 0) {
                softSelect_ = SoftSelect::Skip;
                submitBatches(ViewShade, shadeProgram_, skinnedShadeProgram_, batches_, idb,
                              shadeState, true);
                softSelect_ = SoftSelect::All;
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
            if (!fadeBatches_.empty() && bgfx::isValid(prepassProgram_)) {
                bgfx::setViewMode(ViewTransparent, bgfx::ViewMode::Sequential);
                const uint64_t depthOnly = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
                submitBatches(ViewTransparent, prepassProgram_, skinnedPrepassProgram_,
                              fadeBatches_, idb, depthOnly, false);
                const uint64_t blended = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_EQUAL |
                                         BGFX_STATE_BLEND_ALPHA;
                submitBatches(ViewTransparent, shadeProgram_, skinnedShadeProgram_, fadeBatches_,
                              idb, blended, true);
            }
            // And the solid figures' soft-alpha parts, the same two passes: MU's alpha test and
            // blend with the depth written (EnableAlphaTest, ZzzOpenglUtil.cpp:366-393). The
            // depth pass cuts below the material's threshold, so the nearest kept surface wins,
            // and the shade is blended by the sheet's own alpha (fs_shade, u_material.y's 4) over
            // the finished picture: the Bahamut's fin membrane, seen through as MU draws it.
            if (total > 0 && bgfx::isValid(prepassProgram_)) {
                bgfx::setViewMode(ViewTransparent, bgfx::ViewMode::Sequential);
                softSelect_ = SoftSelect::Only;
                submitBatches(ViewTransparent, prepassProgram_, skinnedPrepassProgram_, batches_,
                              idb, BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS, false);
                submitBatches(ViewTransparent, shadeProgram_, skinnedShadeProgram_, batches_, idb,
                              BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_EQUAL |
                                  BGFX_STATE_BLEND_ALPHA,
                              true);
                softSelect_ = SoftSelect::All;
            }

            // --- view 5, first half: MU's BlendMeshes --------------------------------
            // The glow parts the three passes above skipped, added into the shade target
            // against the prepass's depth and writing none. Here and not in Effects because
            // they are meshes that ride the frame's instance buffer, which lives in this
            // block; the view's target and transform are set below with the sprites'.
            if (total > 0 && bgfx::isValid(glowProgram_) && bgfx::isValid(skinnedGlowProgram_)) {
                // LEQUAL, as MU's GL draws its BlendMesh: a glow shell modelled on its own base
                // -- the 2nd wings' `_R` meshes, the same vertices again -- lies at exactly the
                // depth the base wrote and failed LESS on every pixel (docs/second-wings.md).
                const uint64_t glowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL |
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
    // w: the present stretches a smaller world itself, and sharply (fs_present's sharpStretch).
    const bool stretched = !bgfx::isValid(upscaled_) && width_ < outWidth_;
    const float params[4] = {lighting.ssaoRadius, lighting.ssaoStrength, lighting.exposure,
                             stretched ? 1.0f : 0.0f};
    const bool bloomed = bgfx::isValid(bloomDownProgram_) && bgfx::isValid(bloomUpProgram_);
    const float bloomParams[4] = {0.0f, 0.0f, bloomed ? lighting.bloomStrength : 0.0f, 0.0f};
    bgfx::setUniform(uBloom_, bloomParams);
    // MetalFX hands the present a screen-sized picture, read one to one and already
    // sharpened: the present's own sharpen on top of it is a second one, and draws a dotted
    // grain over flat metal.
    const bool upscaled = bgfx::isValid(upscaled_);
    // Nor on the sharp stretch, which is Catmull-Rom already and whose five reads a sharpen
    // took five times over (1.1 ms at 2K, measured).
    const float present[4] = {upscaled || stretched ? 0.0f : lighting.sharpen, lighting.contrast,
                              1.0f / float(upscaled ? outWidth_ : width_),
                              1.0f / float(upscaled ? outHeight_ : height_)};
    bgfx::setUniform(uPresent_, present);
    const float grade[4] = {lighting.tonemap, lighting.saturation * (1.0f - drain_),
                            lighting.split, dim_};
    const float tintLow[4] = {lighting.tintLow[0], lighting.tintLow[1], lighting.tintLow[2], 0.0f};
    const float tintHigh[4] = {lighting.tintHigh[0], lighting.tintHigh[1], lighting.tintHigh[2],
                               0.0f};
    bgfx::setUniform(uGrade_, grade);
    bgfx::setUniform(uTintLow_, tintLow);
    bgfx::setUniform(uTintHigh_, tintHigh);
    bgfx::setTexture(9, sBloom_, bloomTex_[0]);
    bgfx::setViewFrameBuffer(ViewPresent, BGFX_INVALID_HANDLE);
    // The screen's own size, not the world's: this is the pass that magnifies a scaled world
    // onto the backbuffer, and its rect is the backbuffer. The sharpen's step above stays one
    // texel of the SOURCE, which is what it samples; a step of one screen pixel on a scaled
    // picture reads the same texel twice and sharpens nothing.
    bgfx::setViewRect(ViewPresent, 0, 0, uint16_t(outWidth_), uint16_t(outHeight_));
    bgfx::setViewClear(ViewPresent, BGFX_CLEAR_COLOR, 0x101418ff, 1.0f, 0);
    bgfx::setViewTransform(ViewPresent, nullptr, nullptr);
    bgfx::setUniform(uParams_, params);
    bgfx::setTexture(8, sColour_, upscaled ? upscaled_ : shadeColour_);
    screenPass(ViewPresent, presentProgram_);

    // View 7 is the HUD's, and is submitted empty until sprint 7 fills it. A view bgfx sees
    // nothing in is dropped, and an account with no rows reads as free rather than unbuilt.
    bgfx::setViewFrameBuffer(ViewHud, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(ViewHud, 0, 0, uint16_t(outWidth_), uint16_t(outHeight_));
    bgfx::touch(ViewHud);
}

}  // namespace mu::gfx
