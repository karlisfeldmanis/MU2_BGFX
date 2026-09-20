#include "content/ground.h"

#include <cgltf.h>

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <cmath>
#include <cstring>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"

namespace mu::content {
namespace {

bgfx::VertexLayout g_layout;
bool g_layoutReady = false;

bx::DefaultAllocator g_allocator;

// Reads one of MU's grids into bytes, `channels` apart. The grids are pictures of data:
// height is a single channel, attributes and light are RGB with the value in the red.
std::vector<uint8_t> readGrid(const std::string& path, int* width, int* height, int channel) {
    std::vector<uint8_t> out;
    std::vector<uint8_t> file = core::readFile(path);
    if (file.empty()) return out;

    bimg::ImageContainer* image = bimg::imageParse(&g_allocator, file.data(), uint32_t(file.size()));
    if (!image) {
        core::logError("%s is not an image bimg can read", path.c_str());
        return out;
    }
    // Widened rather than switched on: MU's grids are 8-bit grey or RGB depending on which
    // tool wrote them, and one path through RGBA8 is one path to get wrong.
    bimg::ImageContainer* rgba = image;
    if (image->m_format != bimg::TextureFormat::RGBA8) {
        rgba = bimg::imageConvert(&g_allocator, bimg::TextureFormat::RGBA8, *image, false);
        bimg::imageFree(image);
        if (!rgba) {
            core::logError("%s did not convert to RGBA8", path.c_str());
            return out;
        }
    }
    *width = int(rgba->m_width);
    *height = int(rgba->m_height);
    const uint8_t* src = static_cast<const uint8_t*>(rgba->m_data);
    out.resize(size_t(*width) * size_t(*height));
    for (size_t i = 0; i < out.size(); ++i) out[i] = src[i * 4 + size_t(channel)];
    bimg::imageFree(rgba);
    return out;
}

// One half of a surface out of ground_surfaces.json. Absent is not an error: nine of
// Lorencia's forty-four surfaces are a base with no overlay at all.
bool readLayer(const core::Json& node, const std::string& dir, const core::Json& cooked,
               const std::string& assetsDir, Textures& textures, GroundLayer* out) {
    if (node.isNull()) return false;
    const std::string albedo = node["albedo"].stringOr("");
    if (albedo.empty()) return false;

    // The cooked sheet if the cook has been run, the source .png if it has not. The land's
    // 27 sheets are 1536 square and are half of everything the cook writes, so reading them
    // as BC7 with their mip chains already in them is most of what the cook is for. The
    // manifest is asked rather than the name being rebuilt here: tools/cook.py decides what
    // a cooked file is called and this should not hold a second opinion.
    auto sheet = [&](const std::string& name, TextureRole role, const char* roleName) {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        if (name.empty()) return handle;
        if (!cooked.isNull()) {
            const std::string key = "ground/" + name + ":" + roleName;
            const std::string relative = cooked[key.c_str()].stringOr("");
            if (!relative.empty()) {
                // The manifest's paths are relative to assets/, which is where the town's
                // own meshes name theirs from too.
                handle = textures.load(core::join(assetsDir, relative), role);
                if (bgfx::isValid(handle)) return handle;
            }
        }
        return textures.load(core::join(dir, name), role);
    };

    out->albedo = sheet(albedo, TextureRole::Albedo, "albedo");
    const std::string normal = node["normal"].stringOr("");
    const std::string orm = node["orm"].stringOr("");
    if (!normal.empty()) out->normal = sheet(normal, TextureRole::Normal, "normal");
    if (!orm.empty()) out->orm = sheet(orm, TextureRole::Data, "orm");
    node.readInto("repeat", &out->repeat);
    node.readInto("relief", &out->relief);
    node.readInto("water", &out->water);

    if (!bgfx::isValid(out->albedo)) out->albedo = textures.white();
    if (!bgfx::isValid(out->normal)) out->normal = textures.flatNormal();
    if (!bgfx::isValid(out->orm)) out->orm = textures.neutralOrm();
    return true;
}

// The surface an albedo belongs to, as MU2's pipeline names it: "TileGrass01 1_tiling_hd.png"
// is TileGrass01. The sheet's own variant and its extension are dropped.
std::string surfaceStem(const std::string& albedo) {
    const size_t space = albedo.find(' ');
    if (space != std::string::npos) return albedo.substr(0, space);
    const size_t dot = albedo.rfind('.');
    return dot == std::string::npos ? albedo : albedo.substr(0, dot);
}

// What the glTF material for this surface must be called: `base__overlay`, or the base alone
// where there is no overlay. MU2's pipeline writes both sides from the same pair, so the name
// is the one thing in the glb that says out loud which surface a primitive expects.
std::string materialNameFor(const core::Json& surface) {
    const std::string base = surfaceStem(surface["base"]["albedo"].stringOr(""));
    const std::string overlay = surfaceStem(surface["overlay"]["albedo"].stringOr(""));
    return overlay.empty() ? base : base + "__" + overlay;
}

}  // namespace

const bgfx::VertexLayout& Ground::layout() {
    if (!g_layoutReady) {
        g_layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
            .end();
        g_layoutReady = true;
    }
    return g_layout;
}

bool Ground::readGrids(const std::string& worldDir, const std::string& heightFile,
                       const std::string& attributesFile) {
    int w = 0, h = 0;
    std::vector<uint8_t> raw = readGrid(core::join(worldDir, heightFile), &w, &h, 0);
    if (raw.empty() || w != size_ || h != size_) {
        core::logError("%s is %dx%d, and the world says %d tiles a side", heightFile.c_str(), w, h,
                       size_);
        return false;
    }
    // A height byte is worth height_factor of MU's units, and MU's units are a hundredth of
    // a tile. docs/conventions.md.
    height_.resize(raw.size());
    const float perByte = heightFactor_ / 100.0f;
    for (size_t i = 0; i < raw.size(); ++i) height_[i] = float(raw[i]) * perByte;

    attrs_ = readGrid(core::join(worldDir, attributesFile), &w, &h, 0);
    if (attrs_.empty() || w != size_ || h != size_) {
        core::logError("%s is %dx%d, and the world says %d tiles a side", attributesFile.c_str(), w,
                       h, size_);
        return false;
    }

    float lowest = 1e30f, highest = -1e30f;
    for (float v : height_) {
        lowest = std::min(lowest, v);
        highest = std::max(highest, v);
    }
    // Counted by asking walkable() rather than by testing a bit here. This line used to test
    // 0x04 alone while walkable() rejects 0x04 or 0x08, which is two definitions of "blocked"
    // in one file: they agree on Lorencia only because 0x08 (NoGround) never occurs on it, so
    // the first map that uses NoGround would have had the log quietly under-report against
    // the test the sim actually walks by.
    size_t blocked = 0;
    for (int row = 0; row < size_; ++row) {
        for (int column = 0; column < size_; ++column) {
            if (!walkable(column, row)) ++blocked;
        }
    }
    core::logf("grids %dx%d: height %.2f to %.2f m, %zu of %zu tiles blocked (%.1f%%)", size_,
               size_, lowest, highest, blocked, attrs_.size(),
               100.0 * double(blocked) / double(attrs_.size()));
    return true;
}

bool Ground::load(const std::string& worldDir, const std::string& worldName, Textures& textures) {
    // Loaded onto whatever was here before, which was nothing until a second world existed:
    // parts_ was appended to rather than replaced, so a second load() drew the first world's
    // surfaces again out of a vertex buffer that no longer holds them.
    shutdown();

    const std::string worldJson = core::join(worldDir, worldName + ".json");
    core::Json doc = core::parseJsonFile(worldJson);
    if (doc.isNull()) {
        core::logError("%s did not parse", worldJson.c_str());
        return false;
    }

    size_ = int(doc["size"].numberOr(256.0));
    const float unitsPerTile = float(doc["units_per_tile"].numberOr(100.0));
    heightFactor_ = float(doc["height_factor"].numberOr(1.5));
    // The world is metres and one tile is one metre; the content counts a hundred units to
    // the tile and is divided on the way in. docs/conventions.md.
    metresPerTile_ = unitsPerTile / 100.0f;

    if (!readGrids(worldDir, doc["height"].stringOr("height.png"),
                   doc["attributes"].stringOr("attributes.png"))) {
        return false;
    }

    // --- the surface table ------------------------------------------------------------
    // The cook's manifest, if there is one. Absent is not an error: the land reads its own
    // .png then, which is what every run before sprint 3's cook did.
    const std::string assetsDir = core::directoryOf(core::directoryOf(worldDir));
    const std::string cookedDir = core::join(assetsDir, "cooked/" + worldName);
    const core::Json cookedFile = core::parseJsonFile(core::join(cookedDir, "textures.json"));
    const core::Json cookedManifest = cookedFile["textures"];
    core::logf("ground %s: %s", worldName.c_str(),
               cookedManifest.isNull() ? "no cooked sheets, reading the source png"
                                       : "reading the cook's own sheets");

    const std::string surfacesPath = core::join(worldDir, "ground_surfaces.json");
    core::Json surfaces = core::parseJsonFile(surfacesPath);
    if (surfaces.isNull() || surfaces.size() == 0) {
        core::logError("%s did not parse, or holds no surfaces", surfacesPath.c_str());
        return false;
    }

    // --- the terrain mesh MU2's pipeline already built ---------------------------------
    // Nothing is built from the heightmap here. The glb is one quad a tile with its own four
    // unshared vertices, already in metres and already with rows running -z, cut into one
    // primitive per surface pair.
    const std::string meshPath = core::join(worldDir, worldName + "_ground.glb");
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, meshPath.c_str(), &data) != cgltf_result_success) {
        core::logError("%s is not a glTF cgltf can read", meshPath.c_str());
        return false;
    }
    if (cgltf_load_buffers(&options, data, meshPath.c_str()) != cgltf_result_success) {
        core::logError("%s: its buffers did not load", meshPath.c_str());
        cgltf_free(data);
        return false;
    }
    if (data->meshes_count != 1) {
        core::logError("%s holds %zu meshes and the ground is one", meshPath.c_str(),
                       size_t(data->meshes_count));
        cgltf_free(data);
        return false;
    }

    const cgltf_mesh& mesh = data->meshes[0];
    if (mesh.primitives_count != surfaces.size()) {
        // The primitive's place in the mesh IS its surface index, so a mismatch is not
        // something to carry on through: it would wear the wrong pair everywhere.
        core::logError("%s has %zu parts and ground_surfaces.json has %zu surfaces",
                       meshPath.c_str(), size_t(mesh.primitives_count), surfaces.size());
        cgltf_free(data);
        return false;
    }

    std::vector<GroundVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<float> scratch;
    size_t waterParts = 0;

    for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
        const cgltf_primitive& prim = mesh.primitives[pi];
        if (prim.type != cgltf_primitive_type_triangles) continue;

        const cgltf_accessor* aPos = nullptr;
        const cgltf_accessor* aNormal = nullptr;
        const cgltf_accessor* aUv = nullptr;
        const cgltf_accessor* aColour = nullptr;
        for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
            const cgltf_attribute& attr = prim.attributes[ai];
            switch (attr.type) {
                case cgltf_attribute_type_position: aPos = attr.data; break;
                case cgltf_attribute_type_normal: aNormal = attr.data; break;
                case cgltf_attribute_type_texcoord: if (!aUv) aUv = attr.data; break;
                case cgltf_attribute_type_color: if (!aColour) aColour = attr.data; break;
                default: break;
            }
        }
        if (!aPos || aPos->count == 0) continue;

        const uint32_t baseVertex = uint32_t(vertices.size());
        const size_t count = size_t(aPos->count);
        vertices.resize(baseVertex + count);

        scratch.assign(count * 3, 0.0f);
        cgltf_accessor_unpack_floats(aPos, scratch.data(), count * 3);
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(vertices[baseVertex + i].position, &scratch[i * 3], sizeof(float) * 3);
        }
        if (aNormal && aNormal->count == count) {
            cgltf_accessor_unpack_floats(aNormal, scratch.data(), count * 3);
            for (size_t i = 0; i < count; ++i) {
                std::memcpy(vertices[baseVertex + i].normal, &scratch[i * 3], sizeof(float) * 3);
            }
        } else {
            for (size_t i = 0; i < count; ++i) {
                vertices[baseVertex + i].normal[0] = 0.0f;
                vertices[baseVertex + i].normal[1] = 1.0f;
                vertices[baseVertex + i].normal[2] = 0.0f;
            }
        }
        if (aUv && aUv->count == count) {
            // In TILES, not in [0,1]: it runs 0..256 across Lorencia. (It said 32..245
            // until a review caught it -- primitive 0's own accessor bounds, read as the
            // whole mesh's, which is the very error the commit this line survived was about.) Each half of the
            // surface multiplies it by its own repeat, so one sheet covers two tiles or four.
            scratch.assign(count * 2, 0.0f);
            cgltf_accessor_unpack_floats(aUv, scratch.data(), count * 2);
            for (size_t i = 0; i < count; ++i) {
                vertices[baseVertex + i].uv[0] = scratch[i * 2 + 0];
                vertices[baseVertex + i].uv[1] = scratch[i * 2 + 1];
            }
        }
        if (aColour && aColour->count == count) {
            // Two things in one attribute: rgb is MU's baked TerrainLight, already a lit
            // result and not an albedo, and a is the weight from base to overlay.
            scratch.assign(count * 4, 0.0f);
            cgltf_accessor_unpack_floats(aColour, scratch.data(), count * 4);
            for (size_t i = 0; i < count; ++i) {
                std::memcpy(vertices[baseVertex + i].colour, &scratch[i * 4], sizeof(float) * 4);
            }
        } else {
            for (size_t i = 0; i < count; ++i) {
                GroundVertex& v = vertices[baseVertex + i];
                v.colour[0] = v.colour[1] = v.colour[2] = 1.0f;
                v.colour[3] = 0.0f;
            }
        }

        GroundPart part;
        part.firstIndex = uint32_t(indices.size());
        part.surface = uint32_t(pi);
        if (prim.indices) {
            part.indexCount = uint32_t(prim.indices->count);
            indices.reserve(indices.size() + part.indexCount);
            for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                indices.push_back(baseVertex + uint32_t(cgltf_accessor_read_index(prim.indices, i)));
            }
        } else {
            part.indexCount = uint32_t(count);
            for (uint32_t i = 0; i < part.indexCount; ++i) indices.push_back(baseVertex + i);
        }

        const core::Json& surface = surfaces.at(pi);

        // That primitive i wears surface i was assumed, and only the two COUNTS were checked.
        // A count is not a pairing: reorder two entries in ground_surfaces.json and every
        // check passed while half the map wore the wrong textures, which reads as MU's own
        // art and not as a bug. So both sides of the join are checked instead -- the json
        // states its own index, and the glTF material is named after the pair it was cut for.
        const cgltf_material* m = prim.material;
        const std::string materialName = m && m->name ? m->name : "";
        const std::string expected = materialNameFor(surface);
        const int stated = int(surface["surface"].numberOr(-1.0));
        if (stated != int(pi) || materialName != expected) {
            core::logError("%s primitive %zu wears material '%s', and ground_surfaces.json's "
                           "entry %zu says surface %d and the pair '%s'. The primitive's place "
                           "in the mesh IS its surface, so this is not something to draw",
                           meshPath.c_str(), size_t(pi), materialName.c_str(), size_t(pi),
                           stated, expected.c_str());
            cgltf_free(data);
            return false;
        }
        part.name = materialName;
        part.pairName = expected;

        readLayer(surface["base"], worldDir, cookedManifest, assetsDir, textures, &part.base);
        part.hasOverlay = readLayer(surface["overlay"], worldDir, cookedManifest, assetsDir,
                                    textures, &part.overlay);
        if (!part.hasOverlay) part.overlay = part.base;
        if (part.base.water || part.overlay.water) ++waterParts;

        parts_.push_back(part);
    }

    cgltf_free(data);

    if (vertices.empty() || indices.empty()) {
        core::logError("%s holds no triangles", meshPath.c_str());
        return false;
    }

    indexCount_ = uint32_t(indices.size());
    const bgfx::Memory* vmem =
        bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(GroundVertex)));
    vbh_ = bgfx::createVertexBuffer(vmem, layout());
    const bgfx::Memory* imem =
        bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint32_t)));
    ibh_ = bgfx::createIndexBuffer(imem, BGFX_BUFFER_INDEX32);

    core::logf("ground %s: %zu surfaces, %u triangles, %zu vertices (%.1f MB), %zu of them water",
               worldName.c_str(), parts_.size(), triangleCount(), vertices.size(),
               double(vertices.size() * sizeof(GroundVertex)) / 1e6, waterParts);
    for (const GroundPart& p : parts_) {
        // Both sides of the join, not just the glb's. p.name is the glTF material's name and
        // p.pairName is what ground_surfaces.json's entry of the same index asks for, and
        // this is the one line in the run that would show them disagreeing.
        core::logf("  %2u %-26s json %-26s %6u tris  repeat %.2f/%.2f%s", p.surface,
                   p.name.c_str(), p.pairName.c_str(), p.indexCount / 3, double(p.base.repeat),
                   double(p.overlay.repeat), p.hasOverlay ? "" : "  (base only)");
    }
    return bgfx::isValid(vbh_) && bgfx::isValid(ibh_);
}

float Ground::heightAt(float x, float z) const {
    if (height_.empty() || size_ <= 0) return 0.0f;
    // Column is +x and row is -z, in tiles. docs/conventions.md.
    const float column = x / metresPerTile_;
    const float row = -z / metresPerTile_;
    if (column < 0.0f || row < 0.0f) return 0.0f;

    const int c0 = int(column), r0 = int(row);
    if (c0 >= size_ - 1 || r0 >= size_ - 1) return 0.0f;
    const float fc = column - float(c0), fr = row - float(r0);

    const auto at = [&](int c, int r) { return height_[size_t(r) * size_t(size_) + size_t(c)]; };
    const float top = at(c0, r0) * (1.0f - fc) + at(c0 + 1, r0) * fc;
    const float bottom = at(c0, r0 + 1) * (1.0f - fc) + at(c0 + 1, r0 + 1) * fc;
    return top * (1.0f - fr) + bottom * fr;
}

// The trap: off the map this returns 0, and 0 is MU's value for a tile that is walkable and
// unblocked. So a caller that reads the bits and asks "is anything set?" gets "this is open
// ground" for everywhere that is not ground at all. walkable() does not fall into it because
// it repeats the bounds test itself before looking at any bit, which is also what MU2's
// Terrain.cs does; anything else added here must do the same.
uint8_t Ground::attributesAt(int column, int row) const {
    if (attrs_.empty() || column < 0 || row < 0 || column >= size_ || row >= size_) return 0;
    return attrs_[size_t(row) * size_t(size_) + size_t(column)];
}

bool Ground::walkable(int column, int row) const {
    if (attrs_.empty() || column < 0 || row < 0 || column >= size_ || row >= size_) return false;
    // MU's own bits: 0x04 is NoMove, 0x08 is NoGround. Either one and nothing walks here.
    const uint8_t a = attributesAt(column, row);
    return (a & 0x04) == 0 && (a & 0x08) == 0;
}

void Ground::shutdown() {
    if (bgfx::isValid(vbh_)) bgfx::destroy(vbh_);
    if (bgfx::isValid(ibh_)) bgfx::destroy(ibh_);
    vbh_ = BGFX_INVALID_HANDLE;
    ibh_ = BGFX_INVALID_HANDLE;
    indexCount_ = 0;
    parts_.clear();
    height_.clear();
    attrs_.clear();
}

}  // namespace mu::content
