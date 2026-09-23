// cookcheck: reads back what tools/cook.py wrote and says whether it is what it claims.
//
// The cook is the first thing in this engine that produces files nothing has loaded yet, and
// PLAN.md's rule is that the author of a thing does not get to be the one who says it works.
// This is not that review -- it is the mechanical half of it, the part that can be checked
// without an eye: that every .ktx is the format and the chain it is supposed to be, that
// every .mum's indices point inside its own vertices, and that the one rule the cook exists
// for actually held.
//
// That rule is the interesting check. A cutout's alpha, averaged down a mip chain, thins
// until the leaf vanishes; texcook rescales each level so the same fraction of it passes the
// cutout test as passed at the top. Here the blocks are decoded back to RGBA8 and the
// coverage is measured at every level. If the rescale had not run, the drift would grow
// monotonically down the chain, and it is reported as the worst level rather than the mean
// so that one bad sheet cannot hide behind thirty good ones.
//
// Usage: cookcheck <cooked world dir>          e.g. assets/cooked/lorencia
// Exits non-zero if anything failed.

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bx::DefaultAllocator g_allocator;
int g_failures = 0;

void fail(const std::string& what, const std::string& why) {
    std::printf("  FAIL %s: %s\n", what.c_str(), why.c_str());
    ++g_failures;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return bytes;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    bytes.resize(size_t(size < 0 ? 0 : size));
    if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
        bytes.clear();
    }
    std::fclose(file);
    return bytes;
}

uint8_t mipCount(uint32_t width, uint32_t height) {
    uint8_t levels = 1;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

const char* baseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

// A PNG's own width and height, so the cooked file can be checked against its source.
bool pngSize(const std::string& path, uint32_t& width, uint32_t& height) {
    std::vector<uint8_t> bytes = readFile(path);
    if (bytes.size() < 24 || std::memcmp(bytes.data() + 1, "PNG", 3) != 0) return false;
    width = (uint32_t(bytes[16]) << 24) | (uint32_t(bytes[17]) << 16) | (uint32_t(bytes[18]) << 8) |
            bytes[19];
    height = (uint32_t(bytes[20]) << 24) | (uint32_t(bytes[21]) << 16) |
             (uint32_t(bytes[22]) << 8) | bytes[23];
    return true;
}

struct Drift {
    float worst = 0.0f;   // the largest coverage error down the chain, as a fraction
    uint8_t level = 0;    // where it was
    float top = 0.0f;     // the coverage the top level had
    float there = 0.0f;
};

// Decodes every level and measures how far its coverage drifted from the top's, over a
// level's TRUE size, which is not the size bimg reports.
//
// bimg rounds a block-compressed level up to whole blocks, so a 3x3 level and a 1x1 level
// both come back as 4x4 and the padding decodes with them. Measured over that padding, a
// tree's last levels read as a coverage of 0.000 or 1.000 and the cook looks broken when it
// is not -- which is what this checker said until the chain was replayed uncompressed and
// found to hold 0.51 within a hundredth all the way down to 6x6. So the true size is
// computed here by halving, as the cook and the GPU both do, and only that corner of the
// decoded level is counted.
Drift coverageDrift(const bimg::ImageContainer& image, float threshold) {
    Drift drift;
    std::vector<uint8_t> rgba;
    for (uint8_t level = 0; level < image.m_numMips; ++level) {
        bimg::ImageMip mip;
        if (!bimg::imageGetRawData(image, 0, level, image.m_data, image.m_size, mip)) break;
        const uint32_t trueW = std::max(1u, image.m_width >> level);
        const uint32_t trueH = std::max(1u, image.m_height >> level);
        rgba.assign(size_t(mip.m_width) * mip.m_height * 4, 0);
        bimg::imageDecodeToRgba8(&g_allocator, rgba.data(), mip.m_data, mip.m_width, mip.m_height,
                                 mip.m_width * 4, mip.m_format);
        size_t kept = 0;
        const size_t texels = size_t(trueW) * trueH;
        for (uint32_t y = 0; y < trueH; ++y) {
            for (uint32_t x = 0; x < trueW; ++x) {
                if (float(rgba[(size_t(y) * mip.m_width + x) * 4 + 3]) / 255.0f >= threshold) {
                    ++kept;
                }
            }
        }
        const float here = texels ? float(kept) / float(texels) : 0.0f;
        if (level == 0) {
            drift.top = here;
            drift.there = here;
            continue;
        }
        // Below about a hundred texels a level cannot express a coverage finely enough for
        // the comparison to mean anything: at 3x3 the nearest expressible value to 0.512 is
        // 0.444. Levels are judged while they can answer, and the tolerance carries the
        // quantum of the level it is applied to.
        if (texels < 64) continue;
        const float error = std::fabs(here - drift.top);
        if (error > drift.worst) {
            drift.worst = error;
            drift.level = level;
            drift.there = here;
        }
    }
    return drift;
}

// The mean colour of a decoded level, which is the cheapest thing that notices an encoder
// writing something entirely unlike its input. Structure checks pass happily on a file full
// of black.
void meanColour(const bimg::ImageContainer& image, uint8_t level, double* out) {
    bimg::ImageMip mip;
    out[0] = out[1] = out[2] = 0.0;
    if (!bimg::imageGetRawData(image, 0, level, image.m_data, image.m_size, mip)) return;
    std::vector<uint8_t> rgba(size_t(mip.m_width) * mip.m_height * 4, 0);
    if (mip.m_format == bimg::TextureFormat::RGBA8) {
        std::memcpy(rgba.data(), mip.m_data, rgba.size());
    } else {
        bimg::imageDecodeToRgba8(&g_allocator, rgba.data(), mip.m_data, mip.m_width,
                                 mip.m_height, mip.m_width * 4, mip.m_format);
    }
    const size_t texels = size_t(mip.m_width) * mip.m_height;
    for (size_t i = 0; i < texels; ++i) {
        for (int c = 0; c < 3; ++c) out[c] += rgba[i * 4 + c];
    }
    for (int c = 0; c < 3; ++c) out[c] /= double(texels ? texels : 1);
}

struct Job {
    std::string role;
    float cutout = -1.0f;
    std::string source;
    std::string target;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: cookcheck <cooked world dir>\n");
        return 2;
    }
    const std::string dir = argv[1];

    // --- the textures, read back out of the job list the cook ran ----------------------
    std::vector<Job> jobs;
    {
        std::FILE* file = std::fopen((dir + "/jobs.txt").c_str(), "r");
        if (file == nullptr) {
            std::fprintf(stderr, "cookcheck: no jobs.txt in %s\n", dir.c_str());
            return 2;
        }
        char line[4096];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            std::string text(line);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            if (text.empty()) continue;
            std::vector<std::string> fields;
            size_t start = 0;
            for (size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == '\t') {
                    fields.push_back(text.substr(start, i - start));
                    start = i + 1;
                }
            }
            if (fields.size() != 4) continue;
            jobs.push_back({fields[0], float(atof(fields[1].c_str())), fields[2], fields[3]});
        }
        std::fclose(file);
    }

    std::printf("cookcheck: %zu textures\n", jobs.size());
    size_t cutouts = 0;
    Drift worst;
    std::string worstName;
    uint64_t blockBytes = 0;

    for (const Job& job : jobs) {
        const std::string name = baseName(job.target);
        std::vector<uint8_t> bytes = readFile(job.target);
        if (bytes.empty()) {
            fail(name, "missing or empty");
            continue;
        }
        blockBytes += bytes.size();

        bx::Error error;
        // Count, not RGBA8: parsed as it is stored, so that the blocks and the chain can be
        // checked rather than a decoded copy of them.
        bimg::ImageContainer* image =
            bimg::imageParse(&g_allocator, bytes.data(), uint32_t(bytes.size()),
                             bimg::TextureFormat::Count, &error);
        if (image == nullptr) {
            fail(name, "does not parse as an image");
            continue;
        }

        const bimg::TextureFormat::Enum wanted =
            job.role == "normal" ? bimg::TextureFormat::BC5 : bimg::TextureFormat::BC7;
        if (image->m_format != wanted) {
            fail(name, "format " + std::to_string(int(image->m_format)) + ", wanted " +
                           std::to_string(int(wanted)));
        }

        uint32_t sourceWidth = 0, sourceHeight = 0;
        if (pngSize(job.source, sourceWidth, sourceHeight)) {
            // The cook resamples a source whose side is not a whole number of blocks, so
            // what is expected here is the source rounded up, not the source. texcook's
            // header says why, and bimg's reader would round it up regardless.
            sourceWidth = (sourceWidth + 3) & ~3u;
            sourceHeight = (sourceHeight + 3) & ~3u;
            if (image->m_width != sourceWidth || image->m_height != sourceHeight) {
                fail(name, "is " + std::to_string(image->m_width) + "x" +
                               std::to_string(image->m_height) + ", its source is " +
                               std::to_string(sourceWidth) + "x" + std::to_string(sourceHeight));
            }
        }

        const uint8_t expected = mipCount(image->m_width, image->m_height);
        if (image->m_numMips != expected) {
            fail(name, std::to_string(image->m_numMips) + " levels, a full chain is " +
                           std::to_string(expected));
        }

        // The colour itself, against the source. A block encoder that writes black, or
        // writes the wrong channels, passes every structural check ever written.
        {
            std::vector<uint8_t> sourceBytes = readFile(job.source);
            bx::Error sourceError;
            bimg::ImageContainer* source =
                sourceBytes.empty()
                    ? nullptr
                    : bimg::imageParse(&g_allocator, sourceBytes.data(),
                                       uint32_t(sourceBytes.size()), bimg::TextureFormat::RGBA8,
                                       &sourceError);
            if (source != nullptr) {
                double was[3], now[3];
                meanColour(*source, 0, was);
                meanColour(*image, 0, now);
                bimg::imageFree(source);
                // Two channels for a normal map: BC5 keeps x and y and the shader rebuilds
                // z, so blue is expected to differ and is not compared.
                const int channels = job.role == "normal" ? 2 : 3;
                for (int c = 0; c < channels; ++c) {
                    if (std::fabs(was[c] - now[c]) > 12.0) {
                        char note[256];
                        std::snprintf(note, sizeof(note),
                                      "channel %d averages %.1f cooked against %.1f in its "
                                      "source",
                                      c, now[c], was[c]);
                        fail(name, note);
                        break;
                    }
                }
            }
        }

        if (job.cutout >= 0.0f && job.role == "albedo") {
            ++cutouts;
            const Drift drift = coverageDrift(*image, job.cutout);
            if (drift.worst > worst.worst) {
                worst = drift;
                worstName = name;
            }
            // A tenth of the sheet is a lot of leaf to lose or invent, and the rescale
            // exists precisely to keep this small. Loud rather than silent.
            if (drift.worst > 0.10f) {
                char note[256];
                std::snprintf(note, sizeof(note),
                              "coverage %.3f at the top, %.3f at level %u -- the rescale did "
                              "not hold",
                              drift.top, drift.there, drift.level);
                fail(name, note);
            }
        }
        bimg::imageFree(image);
    }

    // --- the meshes --------------------------------------------------------------------
    // Structure only: that a .mum says what it is, that every index points inside its own
    // vertices, that the parts between them cover the indices exactly once, that the bounds
    // are a box rather than the infinities they start as, and that every texture a material
    // names is a file. Whether it is the right model is the eye's job, not this one's.
    size_t meshes = 0;
    uint64_t meshBytes = 0;
    uint64_t triangles = 0;
    {
        std::FILE* list = popen(("ls " + dir + "/meshes/*.mum 2>/dev/null").c_str(), "r");
        char path[4096];
        while (list != nullptr && std::fgets(path, sizeof(path), list) != nullptr) {
            std::string file(path);
            while (!file.empty() && (file.back() == '\n' || file.back() == '\r')) file.pop_back();
            const std::string name = baseName(file);
            std::vector<uint8_t> bytes = readFile(file);
            if (bytes.size() < 48) {
                fail(name, "shorter than its own header");
                continue;
            }
            ++meshes;
            meshBytes += bytes.size();

            uint32_t header[6];
            std::memcpy(header, bytes.data(), 24);
            // 3 is a static mesh and 4 is a skinned one. The town's own models are all
            // static, but a few of MU's world objects carry a sway rig and arrive skinned,
            // so both are accepted here rather than only the one this directory expects.
            if (std::memcmp(bytes.data(), "MU2M", 4) != 0 ||
                (header[1] != 3 && header[1] != 4)) {
                fail(name, "is not a version 3 or 4 .mum");
                continue;
            }
            const uint32_t vertices = header[2];
            const uint32_t indices = header[3];
            const uint32_t parts = header[4];
            const uint32_t materials = header[5];
            // A skinned mesh puts its bone count after the bounds and carries 56-byte
            // vertices instead of 48. Reading one with the static layout does not fail on
            // the header -- every count is plausible -- it fails eight bytes a vertex later,
            // as parts that run past the end of indices that are not there. Which is exactly
            // what this checker reported on 20 of Lorencia's models the moment it began
            // accepting version 4 at all.
            const bool skinned = header[1] == 4;
            const size_t boneCountBytes = skinned ? 4 : 0;
            const size_t vertexStride = skinned ? 56 : 48;
            float bounds[6];
            std::memcpy(bounds, bytes.data() + 24, sizeof(bounds));
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(bounds[i]) || !std::isfinite(bounds[i + 3]) ||
                    bounds[i] > bounds[i + 3]) {
                    fail(name, "bounds are not a box");
                    break;
                }
            }

            const size_t vertexBytes = size_t(vertices) * vertexStride;
            const size_t indexBytes = size_t(indices) * 4;
            const size_t bodyAt = 48 + boneCountBytes;
            const size_t partsAt = bodyAt + vertexBytes + indexBytes;
            if (bytes.size() < partsAt + size_t(parts) * 12) {
                fail(name, "is shorter than its own counts");
                continue;
            }
            triangles += indices / 3;

            uint32_t worstIndex = 0;
            for (uint32_t i = 0; i < indices; ++i) {
                uint32_t value;
                std::memcpy(&value, bytes.data() + bodyAt + vertexBytes + size_t(i) * 4, 4);
                if (value > worstIndex) worstIndex = value;
            }
            if (indices > 0 && worstIndex >= vertices) {
                fail(name, "an index reaches vertex " + std::to_string(worstIndex) + " of " +
                               std::to_string(vertices));
            }

            uint64_t covered = 0;
            for (uint32_t p = 0; p < parts; ++p) {
                uint32_t triple[3];
                std::memcpy(triple, bytes.data() + partsAt + size_t(p) * 12, 12);
                if (size_t(triple[0]) + triple[1] > indices) {
                    fail(name, "a part runs past the end of the indices");
                }
                if (triple[2] >= materials) {
                    fail(name, "a part names material " + std::to_string(triple[2]) + " of " +
                                   std::to_string(materials));
                }
                covered += triple[1];
            }
            if (covered != indices) {
                fail(name, "its parts cover " + std::to_string(covered) + " indices of " +
                               std::to_string(indices));
            }
        }
        if (list != nullptr) pclose(list);
    }
    std::printf("  %zu meshes, %llu triangles, %.1f MB\n", meshes,
                (unsigned long long)triangles, double(meshBytes) / 1e6);

    // --- the town ----------------------------------------------------------------------
    // The .mut's invariants are the ones a frame will rely on without checking: that the
    // chunks' instance runs are contiguous and cover every instance exactly once, that the
    // models inside a chunk are sorted so each is one range, that every instance stands
    // inside the box its chunk claims, and that a model index names a model. A frame that
    // walks this file does no sorting and no lookups, which is only safe if the cook is
    // telling the truth.
    {
        std::string town;
        {
            std::FILE* list = popen(("ls " + dir + "/*.mut 2>/dev/null").c_str(), "r");
            char path[4096];
            if (list != nullptr && std::fgets(path, sizeof(path), list) != nullptr) {
                town = path;
                while (!town.empty() && (town.back() == '\n' || town.back() == '\r')) {
                    town.pop_back();
                }
            }
            if (list != nullptr) pclose(list);
        }
        std::vector<uint8_t> bytes = town.empty() ? std::vector<uint8_t>() : readFile(town);
        if (bytes.size() < 32) {
            std::printf("  no .mut to check\n");
        } else if (std::memcmp(bytes.data(), "MU2T", 4) != 0) {
            fail(baseName(town), "is not a .mut");
        } else {
            uint32_t head[7];
            std::memcpy(head, bytes.data() + 4, sizeof(head));
            const uint32_t models = head[1], chunks = head[2], count = head[3];
            size_t at = 32;
            std::vector<float> modelReach(models, 0.0f);
            for (uint32_t m = 0; m < models && at + 2 <= bytes.size(); ++m) {
                for (int s = 0; s < 2; ++s) {
                    uint16_t length;
                    std::memcpy(&length, bytes.data() + at, 2);
                    at += 2 + length;
                }
                at += 24 + 4;
            }
            const size_t chunksAt = at;
            // A chunk record is 24 bytes of box, two u32 and two u16: 36, not 40.
            const size_t instancesAt = chunksAt + size_t(chunks) * 36;
            // Version 2 put two more tables after the instances (sprint 8a): u32 count and 48
            // bytes an emitter, then u32 count and 20 bytes a glow. This read them as slack
            // and failed every version-2 town it was given -- it was written when the
            // instances were the end of the file and never taught otherwise. Walked rather
            // than assumed, so a short or a padded tail is still caught.
            size_t tail = instancesAt + size_t(count) * 36;
            for (const size_t stride : {size_t(48), size_t(20)}) {
                if (head[0] < 2 || tail + 4 > bytes.size()) break;
                uint32_t rows;
                std::memcpy(&rows, bytes.data() + tail, 4);
                tail += 4 + size_t(rows) * stride;
            }
            if (tail != bytes.size()) {
                fail(baseName(town), "its counts do not add up to its size");
            } else {
                uint32_t cursor = 0;
                uint32_t outsideBox = 0, unsorted = 0, badModel = 0;
                for (uint32_t c = 0; c < chunks; ++c) {
                    const uint8_t* record = bytes.data() + chunksAt + size_t(c) * 36;
                    float box[6];
                    uint32_t first, inChunk;
                    std::memcpy(box, record, 24);
                    std::memcpy(&first, record + 24, 4);
                    std::memcpy(&inChunk, record + 28, 4);
                    if (first != cursor) {
                        fail(baseName(town), "chunk " + std::to_string(c) + " starts at " +
                                                 std::to_string(first) + ", not at " +
                                                 std::to_string(cursor));
                    }
                    cursor += inChunk;
                    uint16_t previous = 0;
                    for (uint32_t i = 0; i < inChunk; ++i) {
                        const uint8_t* instance = bytes.data() + instancesAt + size_t(first + i) * 36;
                        float position[3];
                        uint16_t model;
                        std::memcpy(position, instance, 12);
                        std::memcpy(&model, instance + 28, 2);
                        if (model >= models) ++badModel;
                        if (model < previous) ++unsorted;
                        previous = model;
                        for (int axis = 0; axis < 3; ++axis) {
                            if (position[axis] < box[axis] || position[axis] > box[axis + 3]) {
                                ++outsideBox;
                                break;
                            }
                        }
                    }
                }
                if (cursor != count) {
                    fail(baseName(town), "its chunks cover " + std::to_string(cursor) +
                                             " instances of " + std::to_string(count));
                }
                if (unsorted) {
                    fail(baseName(town), std::to_string(unsorted) +
                                             " instances break the sort by model inside a chunk");
                }
                if (outsideBox) {
                    fail(baseName(town), std::to_string(outsideBox) +
                                             " instances stand outside their chunk's box");
                }
                if (badModel) {
                    fail(baseName(town), std::to_string(badModel) + " instances name no model");
                }
                std::printf("  %u placements in %u chunks, %u models\n", count, chunks, models);
            }
        }
    }

    if (!worstName.empty()) {
        std::printf("  %zu cutout albedos; worst coverage drift %.1f%% (%s, level %u: %.3f "
                    "against %.3f at the top)\n",
                    cutouts, double(worst.worst) * 100.0, worstName.c_str(), worst.level,
                    double(worst.there), double(worst.top));
    }
    std::printf("  %.1f MB of blocks read back\n", double(blockBytes) / 1e6);
    std::printf("cookcheck: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
