// texcook: one PNG into one .ktx with BC blocks and a full mip chain, in batches.
//
// This is half of sprint 3's cook. It exists because the engine currently decodes 587
// embedded PNGs and builds their mip chains at load, which is the load time and 297 MB of
// RGBA8 in VRAM, and because the one rule docs/conventions.md records as *owed* -- a
// cutout's alpha rescaled down the chain so a leaf keeps its coverage -- cannot be
// expressed through bimg's texturec, which builds its chain internally.
//
// It is a tool, not a library: it links bx and bimg and knows nothing about the game. The
// driver is tools/cook.py, which decides what each image is for.
//
// Three things about the pinned bimg, each found by reading its source rather than by
// guessing, and each of which silently writes a wrong file if ignored:
//
//   1. imageEncodeFromRgba8 REFUSES BC7. Its BC7 case sets an error and writes nothing; BC7
//      goes through imageEncodeFromRgba32f, which is nvtt's own compressor. BC5 is the
//      other way round -- squish, from RGBA8. So the two formats this cook writes take two
//      different paths, and the float one must not be handed linearised colour: RGBA32F
//      here means bytes over 255, because an albedo's bytes ARE sRGB and the hardware
//      decodes them on the sample.
//   2. Nothing pads. A block encoder is handed the level's own width, and Lorencia has
//      sheets 6 and 12 and 24 texels wide, plus the tail of every chain at 2x2 and 1x1. A
//      level is padded here to a multiple of four by clamping its edge, and the KTX is
//      still written with the true size: the block count is the same either way, so the
//      padding changes what is inside the last block and nothing else.
//   3. A level is half the one above it rounded DOWN, minimum one. That is what bgfx and
//      bimg compute when they walk a chain, and a level sized differently from theirs is
//      read at the wrong offset.
//
// Usage: texcook jobs.txt [--threads N]
// where each line of jobs.txt is, tab separated:
//     role<TAB>cutout<TAB>input.png<TAB>output.ktx
// role is albedo, emissive, normal or orm; cutout is the alpha threshold a cutout's
// coverage is held at, or -1 for no rescale.

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/file.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// Threads one image's BC7 level is split across: the cores the job pool leaves idle. A full
// cook has hundreds of images and no core to spare, so each runs whole; cook_one's handful
// of jobs gives each image most of the machine. Set in main before the pool starts.
uint32_t g_stripThreads = 1;  // set in main: every core
BX_ERROR_RESULT(kStripRefused, BX_MAKEFOURCC('M', 'U', 'S', 'R'));

enum class Role { Albedo, Emissive, Normal, Orm };

struct Job {
    Role role = Role::Albedo;
    float cutout = -1.0f;
    std::string input;
    std::string output;
};

bool isSrgb(Role role) { return role == Role::Albedo || role == Role::Emissive; }

// Normals are two channels and the shader rebuilds z; everything else keeps four.
bimg::TextureFormat::Enum formatFor(Role role) {
    return role == Role::Normal ? bimg::TextureFormat::BC5 : bimg::TextureFormat::BC7;
}

const float* srgbToLinearTable() {
    static float table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i) {
            const float c = float(i) / 255.0f;
            table[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        ready = true;
    }
    return table;
}

uint8_t linearToSrgbByte(float v) {
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return uint8_t(s * 255.0f + 0.5f);
}

// One level down by a 2x2 box, in the space the texels stand for: linear light for an
// albedo, a signed vector renormalised for a normal, the stored value for anything else.
// This is content/texture.cpp's own downsample, moved here because once the cook exists the
// engine has no business building a chain at all. The orphan row and column at an odd size
// go into the last destination pixel rather than being dropped, for the same reason as there.
void downsample(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, uint32_t dstW,
                uint32_t dstH, Role role) {
    const float* toLinear = srgbToLinearTable();
    const bool srgb = isSrgb(role);
    const bool normal = role == Role::Normal;

    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t y0 = y * 2;
        const uint32_t yEnd = (y + 1 == dstH) ? srcH - 1 : (y0 + 1 < srcH ? y0 + 1 : srcH - 1);
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t x0 = x * 2;
            const uint32_t xEnd = (x + 1 == dstW) ? srcW - 1 : (x0 + 1 < srcW ? x0 + 1 : srcW - 1);
            const float inv = 1.0f / float((yEnd - y0 + 1) * (xEnd - x0 + 1));

            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (uint32_t sy = y0; sy <= yEnd; ++sy) {
                for (uint32_t sx = x0; sx <= xEnd; ++sx) {
                    const uint8_t* q = src + (size_t(sy) * srcW + sx) * 4;
                    if (srgb) {
                        acc[0] += toLinear[q[0]];
                        acc[1] += toLinear[q[1]];
                        acc[2] += toLinear[q[2]];
                    } else if (normal) {
                        acc[0] += float(q[0]) / 127.5f - 1.0f;
                        acc[1] += float(q[1]) / 127.5f - 1.0f;
                        acc[2] += float(q[2]) / 127.5f - 1.0f;
                    } else {
                        acc[0] += float(q[0]);
                        acc[1] += float(q[1]);
                        acc[2] += float(q[2]);
                    }
                    acc[3] += float(q[3]);
                }
            }
            for (float& v : acc) v *= inv;

            uint8_t* out = dst + (size_t(y) * dstW + x) * 4;
            if (srgb) {
                out[0] = linearToSrgbByte(acc[0]);
                out[1] = linearToSrgbByte(acc[1]);
                out[2] = linearToSrgbByte(acc[2]);
            } else if (normal) {
                const float len = std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
                const float nx = len > 1e-6f ? acc[0] / len : 0.0f;
                const float ny = len > 1e-6f ? acc[1] / len : 0.0f;
                const float nz = len > 1e-6f ? acc[2] / len : 1.0f;
                out[0] = uint8_t((nx + 1.0f) * 127.5f + 0.5f);
                out[1] = uint8_t((ny + 1.0f) * 127.5f + 0.5f);
                out[2] = uint8_t((nz + 1.0f) * 127.5f + 0.5f);
            } else {
                out[0] = uint8_t(acc[0] + 0.5f);
                out[1] = uint8_t(acc[1] + 0.5f);
                out[2] = uint8_t(acc[2] + 0.5f);
            }
            out[3] = uint8_t(acc[3] + 0.5f);
        }
    }
}

// What fraction of a level survives the cutout test, with alpha scaled by `scale`.
float coverage(const std::vector<uint8_t>& level, float threshold, float scale) {
    const size_t texels = level.size() / 4;
    if (texels == 0) return 0.0f;
    size_t kept = 0;
    for (size_t i = 0; i < texels; ++i) {
        if (float(level[i * 4 + 3]) / 255.0f * scale >= threshold) ++kept;
    }
    return float(kept) / float(texels);
}

// The rule docs/conventions.md records as owed. Averaging alpha down a chain thins a leaf
// until it vanishes at distance -- a grass blade covering a third of its texels at the top
// level covers a tenth of them four levels down, and the failure reads as "the draw range
// is too short" rather than as a texture bug. So each level's alpha is scaled until the
// same fraction of it passes the cutout test as passed at the top, by bisection because
// coverage is a step function of the scale and nothing solves it in closed form.
void holdCoverage(std::vector<uint8_t>& level, float threshold, float wanted) {
    if (wanted <= 0.0f || wanted >= 1.0f) return;
    float lo = 0.0f;
    float hi = 16.0f;
    float scale = 1.0f;
    for (int step = 0; step < 24; ++step) {
        scale = (lo + hi) * 0.5f;
        if (coverage(level, threshold, scale) < wanted) {
            lo = scale;
        } else {
            hi = scale;
        }
    }
    scale = (lo + hi) * 0.5f;
    if (std::fabs(scale - 1.0f) < 1e-3f) return;
    for (size_t i = 3; i < level.size(); i += 4) {
        const float a = float(level[i]) * scale;
        level[i] = uint8_t(a < 0.0f ? 0.0f : (a > 255.0f ? 255.0f : a) + 0.5f);
    }
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

// A level padded out to whole blocks by repeating its edge. The block count is unchanged,
// so only the inside of the last block moves -- and an encoder handed a 6-wide level
// without this reads past the end of the row it was given.
void padToBlocks(const std::vector<uint8_t>& src, uint32_t w, uint32_t h,
                 std::vector<uint8_t>& dst, uint32_t pw, uint32_t ph) {
    dst.resize(size_t(pw) * ph * 4);
    for (uint32_t y = 0; y < ph; ++y) {
        const uint32_t sy = y < h ? y : h - 1;
        for (uint32_t x = 0; x < pw; ++x) {
            const uint32_t sx = x < w ? x : w - 1;
            std::memcpy(&dst[(size_t(y) * pw + x) * 4], &src[(size_t(sy) * w + sx) * 4], 4);
        }
    }
}

struct Result {
    bool ok = false;
    uint64_t sourceBytes = 0;
    uint64_t cookedBytes = 0;
    uint64_t rgbaBytes = 0;  // what this texture would have cost in VRAM uncooked, top level
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t mips = 0;
    std::string note;
};

Result cook(const Job& job, bx::AllocatorI* allocator) {
    Result result;

    bx::FileReader reader;
    bx::Error err;
    if (!bx::open(&reader, job.input.c_str(), &err)) {
        result.note = "cannot open";
        return result;
    }
    const int64_t size = bx::getSize(&reader);
    std::vector<uint8_t> raw;
    raw.resize(size_t(size));
    bx::read(&reader, raw.data(), int32_t(size), &err);
    bx::close(&reader);
    result.sourceBytes = uint64_t(size);

    bimg::ImageContainer* parsed = bimg::imageParse(allocator, raw.data(), uint32_t(raw.size()),
                                                    bimg::TextureFormat::RGBA8, &err);
    if (parsed == nullptr) {
        result.note = "not an image bimg reads";
        return result;
    }

    uint32_t width = parsed->m_width;
    uint32_t height = parsed->m_height;
    result.width = width;
    result.height = height;
    result.rgbaBytes = uint64_t(width) * height * 4;

    bimg::ImageMip top;
    if (!bimg::imageGetRawData(*parsed, 0, 0, parsed->m_data, parsed->m_size, top)) {
        bimg::imageFree(parsed);
        result.note = "no level 0";
        return result;
    }
    std::vector<uint8_t> level(top.m_data, top.m_data + size_t(width) * height * 4);
    bimg::imageFree(parsed);

    // A level whose side is not a multiple of four is RESAMPLED to one, not padded.
    //
    // Padding was the first answer and it is wrong, found by cookcheck rather than by
    // thinking: bimg's KTX *reader* rounds a block-compressed image's size up to whole
    // blocks even when the file records the true size, so a 192x6 sheet comes back as
    // 192x8. Edge-padded, its six rows of content then live in eight rows of texture and
    // every v coordinate lands a quarter of the way off. Resampled, the content spans what
    // the reader will say the texture is, and the only cost is a little softening on the
    // four sheets in Lorencia that are built this way.
    if ((width & 3) || (height & 3)) {
        const uint32_t rw = (width + 3) & ~3u;
        const uint32_t rh = (height + 3) & ~3u;
        std::vector<uint8_t> resampled(size_t(rw) * rh * 4);
        for (uint32_t y = 0; y < rh; ++y) {
            const float sy = (float(y) + 0.5f) * float(height) / float(rh) - 0.5f;
            const int y0 = int(std::floor(sy));
            const float fy = sy - float(y0);
            for (uint32_t x = 0; x < rw; ++x) {
                const float sx = (float(x) + 0.5f) * float(width) / float(rw) - 0.5f;
                const int x0 = int(std::floor(sx));
                const float fx = sx - float(x0);
                for (int c = 0; c < 4; ++c) {
                    float total = 0.0f;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int px = std::min(std::max(x0 + dx, 0), int(width) - 1);
                            const int py = std::min(std::max(y0 + dy, 0), int(height) - 1);
                            const float weight = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
                            total += weight * float(level[(size_t(py) * width + px) * 4 + c]);
                        }
                    }
                    resampled[(size_t(y) * rw + x) * 4 + c] = uint8_t(total + 0.5f);
                }
            }
        }
        level.swap(resampled);
        result.width = width = rw;
        result.height = height = rh;
        result.note = "resampled to whole blocks";
    }

    const float wanted = job.cutout >= 0.0f ? coverage(level, job.cutout, 1.0f) : -1.0f;

    const bimg::TextureFormat::Enum format = formatFor(job.role);
    const uint8_t mips = mipCount(width, height);
    result.mips = mips;

    // Every level's blocks, end to end, which is the layout imageWriteKtx expects.
    std::vector<uint8_t> blocks;
    std::vector<uint8_t> padded;
    std::vector<uint8_t> next;
    uint32_t w = width;
    uint32_t h = height;

    // `level` is the chain as it is filtered, and is NEVER the rescaled one. The rescale is
    // applied to a copy on its way to the encoder.
    //
    // Getting that wrong is the whole trap, and this code had it wrong until cookcheck read
    // the files back: rescaling in place means the next level is filtered from alpha that
    // has already been pushed once, and the push compounds. Fourteen of Lorencia's
    // twenty-four cutout sheets ran away with it -- every tree among them -- some to a
    // coverage of 1.000 where the leaves fill the sheet, some to 0.000 where they vanish
    // altogether, which are the two failures this rescale exists to prevent.
    std::vector<uint8_t> shaped;

    for (uint8_t level_i = 0; level_i < mips; ++level_i) {
        if (level_i > 0) {
            const uint32_t nw = w > 1 ? w / 2 : 1;
            const uint32_t nh = h > 1 ? h / 2 : 1;
            next.assign(size_t(nw) * nh * 4, 0);
            downsample(level.data(), w, h, next.data(), nw, nh, job.role);
            level.swap(next);
            w = nw;
            h = nh;
        }

        const std::vector<uint8_t>* encoded = &level;
        if (wanted > 0.0f && level_i > 0) {
            shaped = level;
            holdCoverage(shaped, job.cutout, wanted);
            encoded = &shaped;
        }

        const uint32_t pw = (w + 3) & ~3u;
        const uint32_t ph = (h + 3) & ~3u;
        const uint8_t* source = encoded->data();
        if (pw != w || ph != h) {
            padToBlocks(*encoded, w, h, padded, pw, ph);
            source = padded.data();
        }

        const uint32_t levelBytes =
            bimg::imageGetSize(nullptr, uint16_t(w), uint16_t(h), 1, false, false, 1, format);
        const size_t at = blocks.size();
        blocks.resize(at + levelBytes);

        bx::Error encodeError;
        if (format == bimg::TextureFormat::BC7) {
            // nvtt's compressor, and the one that must NOT see linearised colour: these
            // floats are the stored bytes over 255, sRGB or not, because the sampler is
            // what decodes them.
            std::vector<float> floats(size_t(pw) * ph * 4);
            for (size_t i = 0; i < floats.size(); ++i) floats[i] = float(source[i]) / 255.0f;
            // In strips of block rows, one thread each. nvtt's BC7 is exhaustive and one
            // 1024-square ORM took six minutes on one core while nine sat idle. A block is
            // encoded from its own sixteen texels and nothing else, and a level's blocks are
            // stored a block row after a block row, so strips encoded apart and laid end to
            // end are the same bytes as the whole encoded at once.
            const uint32_t blockRows = ph / 4;
            const uint32_t rowBytes = (pw / 4) * 16;
            const uint32_t strips = std::max(1u, std::min(g_stripThreads, blockRows));
            const uint32_t perStrip = (blockRows + strips - 1) / strips;
            std::atomic<bool> refused{false};
            auto strip = [&](uint32_t s, bx::AllocatorI* own) {
                const uint32_t first = s * perStrip;
                if (first >= blockRows) return;
                const uint32_t rows = std::min(perStrip, blockRows - first);
                bx::Error stripError;
                bimg::imageEncodeFromRgba32f(own, &blocks[at + size_t(first) * rowBytes],
                                             &floats[size_t(first) * 4 * pw * 4], pw, rows * 4, 1,
                                             format, bimg::Quality::Default, &stripError);
                if (!stripError.isOk()) refused = true;
            };
            std::vector<std::thread> helpers;
            for (uint32_t s = 1; s < strips; ++s) {
                helpers.emplace_back([&, s] {
                    bx::DefaultAllocator own;
                    strip(s, &own);
                });
            }
            strip(0, allocator);
            for (std::thread& helper : helpers) helper.join();
            if (refused) encodeError.setError(kStripRefused, "BC7 strip refused");
        } else {
            bimg::imageEncodeFromRgba8(allocator, &blocks[at], source, pw, ph, 1, format,
                                       job.role == Role::Normal ? bimg::Quality::NormalMapDefault
                                                                : bimg::Quality::Default,
                                       &encodeError);
        }
        if (!encodeError.isOk()) {
            result.note = "encode refused";
            return result;
        }
    }

    bx::FileWriter writer;
    if (!bx::open(&writer, job.output.c_str(), false, &err)) {
        result.note = "cannot write";
        return result;
    }
    bimg::imageWriteKtx(&writer, format, false, uint16_t(width), uint16_t(height), 1, mips, 1,
                        isSrgb(job.role), blocks.data(), &err);
    bx::close(&writer);

    result.ok = err.isOk();
    result.cookedBytes = blocks.size();
    if (!result.ok) result.note = "ktx write failed";
    return result;
}

Role roleFrom(const std::string& text) {
    if (text == "normal") return Role::Normal;
    if (text == "orm") return Role::Orm;
    if (text == "emissive") return Role::Emissive;
    return Role::Albedo;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: texcook jobs.txt [--threads N]\n");
        return 2;
    }
    unsigned threadCount = std::thread::hardware_concurrency();
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--threads") == 0) threadCount = unsigned(atoi(argv[i + 1]));
    }
    if (threadCount < 1) threadCount = 1;

    std::vector<Job> jobs;
    {
        std::FILE* file = std::fopen(argv[1], "r");
        if (file == nullptr) {
            std::fprintf(stderr, "texcook: cannot read %s\n", argv[1]);
            return 2;
        }
        char line[4096];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            std::string text(line);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            if (text.empty() || text[0] == '#') continue;
            std::vector<std::string> fields;
            size_t start = 0;
            for (size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == '\t') {
                    fields.push_back(text.substr(start, i - start));
                    start = i + 1;
                }
            }
            if (fields.size() != 4) {
                std::fprintf(stderr, "texcook: %s is not four fields\n", text.c_str());
                return 2;
            }
            Job job;
            job.role = roleFrom(fields[0]);
            job.cutout = float(atof(fields[1].c_str()));
            job.input = fields[2];
            job.output = fields[3];
            jobs.push_back(job);
        }
        std::fclose(file);
    }

    // Every BC7 level is split across all the cores, whatever the pool is doing: the pool's
    // threads and the strips' share the machine and the scheduler keeps it full. Dividing the
    // cores between the jobs left them idle whenever a fast job finished beside a slow one,
    // and with more jobs than cores the last 1024-square ran alone on one core for minutes.
    g_stripThreads = threadCount;

    std::vector<Result> results(jobs.size());
    std::atomic<size_t> nextJob{0};
    std::atomic<size_t> failures{0};

    auto run = [&]() {
        // One allocator a thread: bx's default allocator is not documented as shared-safe
        // and the encoders allocate inside.
        bx::DefaultAllocator allocator;
        for (;;) {
            const size_t index = nextJob.fetch_add(1);
            if (index >= jobs.size()) return;
            results[index] = cook(jobs[index], &allocator);
            if (!results[index].ok) {
                ++failures;
                std::fprintf(stderr, "texcook: %s: %s\n", jobs[index].input.c_str(),
                             results[index].note.c_str());
            }
        }
    };

    std::vector<std::thread> pool;
    for (unsigned i = 1; i < threadCount; ++i) pool.emplace_back(run);
    run();
    for (std::thread& thread : pool) thread.join();

    uint64_t source = 0;
    uint64_t cooked = 0;
    uint64_t rgba = 0;
    for (const Result& r : results) {
        source += r.sourceBytes;
        cooked += r.cookedBytes;
        rgba += r.rgbaBytes;
    }
    // The uncooked figure is the top level only, which is what the engine uploads today
    // before it builds its chain; a cooked chain is a third more texels and still smaller.
    std::printf("texcook: %zu images, %zu failed, %.1f MB in, %.1f MB of blocks with mips, "
                "against %.1f MB of RGBA8 at the top level alone\n",
                jobs.size(), size_t(failures.load()), double(source) / 1e6, double(cooked) / 1e6,
                double(rgba) / 1e6);
    return failures.load() > 0 ? 1 : 0;
}
