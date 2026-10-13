// The packer: one .mupak from a list of files (core/pack.h has the format).
//
//     mupack OUT.mupak LIST [--level N] [--threads N]
//     mupack --check DIR LIST      mount DIR's packs as the game does; every listed file must
//                                  read back byte for byte as its source
//
// LIST is a line a file, "logical path<TAB>source path": the path the game will ask for under
// the pack's folder ("assets/cooked/lorencia/lorencia.mur") and the file on disk it is made
// from. `./mu dist` writes the lists (tools/mu/dist.py) and runs this once a pack.
//
// Each file is compressed on its own, so the game reads one without the rest; a file zstd
// cannot shrink by a fortieth (the BC7 textures come close) is stored as it is, which costs
// nothing to read. Files are compressed on a few threads in batches and written in the list's
// order, so a pack is the same bytes however many threads made it.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <zstd.h>

#include "core/files.h"
#include "core/pack.h"

using namespace mu;

namespace {

struct Item {
    std::string name;
    std::string source;
    std::vector<uint8_t> bytes;  // compressed or stored, before the scramble
    uint32_t size = 0;
    uint8_t method = core::kPackStored;
    bool ok = false;
};

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
}
void put64(std::vector<uint8_t>& out, uint64_t v) {
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 8);
}

void squeeze(Item& item, int level) {
    std::vector<uint8_t> raw = core::readFile(item.source);
    if (raw.empty()) return;
    item.size = uint32_t(raw.size());
    std::vector<uint8_t> packed(ZSTD_compressBound(raw.size()));
    const size_t n = ZSTD_compress(packed.data(), packed.size(), raw.data(), raw.size(), level);
    if (!ZSTD_isError(n) && n + n / 40 < raw.size()) {
        packed.resize(n);
        item.bytes = std::move(packed);
        item.method = core::kPackZstd;
    } else {
        item.bytes = std::move(raw);
        item.method = core::kPackStored;
    }
    item.ok = true;
}

}  // namespace

// The round trip, through the game's own reader: mounted as core::mountPacks mounts, read as
// core::readFile reads, compared with the file it was made from.
int check(const std::string& dir, const std::string& listPath) {
    if (core::mountPacks(dir) == 0) {
        std::fprintf(stderr, "mupack: no packs mount in %s\n", dir.c_str());
        return 1;
    }
    size_t checked = 0, wrong = 0;
    std::ifstream list(listPath);
    for (std::string line; std::getline(list, line);) {
        const size_t tab = line.find('\t');
        if (line.empty() || tab == std::string::npos) continue;
        const std::string name = dir + "/" + line.substr(0, tab);
        std::vector<uint8_t> fromPack;
        if (!core::packRead(name, fromPack) || fromPack != core::readFile(line.substr(tab + 1))) {
            std::fprintf(stderr, "mupack: %s does not read back\n", line.substr(0, tab).c_str());
            ++wrong;
        }
        ++checked;
    }
    std::printf("mupack: %zu files checked, %zu wrong\n", checked, wrong);
    return wrong == 0 && checked > 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc == 4 && !std::strcmp(argv[1], "--check")) return check(argv[2], argv[3]);
    if (argc < 3) {
        std::fprintf(stderr, "mupack OUT.mupak LIST [--level N] [--threads N]\n");
        return 2;
    }
    const std::string outPath = argv[1];
    int level = 12, threads = 6;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--level")) level = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--threads")) threads = std::max(1, std::atoi(argv[i + 1]));
    }

    std::vector<Item> items;
    std::ifstream list(argv[2]);
    for (std::string line; std::getline(list, line);) {
        const size_t tab = line.find('\t');
        if (line.empty() || tab == std::string::npos) continue;
        items.push_back({line.substr(0, tab), line.substr(tab + 1), {}, 0, 0, false});
    }
    if (items.empty()) {
        std::fprintf(stderr, "mupack: %s lists nothing\n", argv[2]);
        return 1;
    }

    const std::string partial = outPath + ".partial";
    FILE* out = std::fopen(partial.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "mupack: %s does not open for writing\n", partial.c_str());
        return 1;
    }
    std::vector<uint8_t> header(32, 0);
    std::fwrite(header.data(), 1, header.size(), out);
    uint64_t at = header.size();
    uint64_t raw = 0;
    std::vector<uint8_t> toc;
    size_t failed = 0;

    const size_t batch = 64;
    for (size_t first = 0; first < items.size(); first += batch) {
        const size_t last = std::min(items.size(), first + batch);
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([&, t] {
                for (size_t i = first + size_t(t); i < last; i += size_t(threads)) {
                    squeeze(items[i], level);
                }
            });
        }
        for (std::thread& worker : workers) worker.join();
        for (size_t i = first; i < last; ++i) {
            Item& item = items[i];
            if (!item.ok) {
                std::fprintf(stderr, "mupack: %s (%s) did not read\n", item.name.c_str(),
                             item.source.c_str());
                ++failed;
                continue;
            }
            core::packScramble(item.bytes.data(), item.bytes.size(), at);
            std::fwrite(item.bytes.data(), 1, item.bytes.size(), out);
            const uint16_t length = uint16_t(item.name.size());
            toc.insert(toc.end(), reinterpret_cast<const uint8_t*>(&length),
                       reinterpret_cast<const uint8_t*>(&length) + 2);
            toc.insert(toc.end(), item.name.begin(), item.name.end());
            put64(toc, at);
            put32(toc, uint32_t(item.bytes.size()));
            put32(toc, item.size);
            toc.push_back(item.method);
            at += item.bytes.size();
            raw += item.size;
            item.bytes = {};
        }
    }

    std::vector<uint8_t> tocPacked(ZSTD_compressBound(toc.size()));
    tocPacked.resize(ZSTD_compress(tocPacked.data(), tocPacked.size(), toc.data(), toc.size(), 19));
    const uint64_t tocAt = at;
    core::packScramble(tocPacked.data(), tocPacked.size(), tocAt);
    std::fwrite(tocPacked.data(), 1, tocPacked.size(), out);

    header.clear();
    put32(header, core::kPackMagic);
    put32(header, core::kPackVersion);
    put32(header, uint32_t(items.size() - failed));
    put32(header, uint32_t(tocPacked.size()));
    put32(header, uint32_t(toc.size()));
    put32(header, 0);
    put64(header, tocAt);
    std::fseek(out, 0, SEEK_SET);
    std::fwrite(header.data(), 1, header.size(), out);
    const bool written = std::fclose(out) == 0;
    if (!written || failed > 0) {
        std::remove(partial.c_str());
        std::fprintf(stderr, "mupack: %s not written (%zu files did not read)\n", outPath.c_str(),
                     failed);
        return 1;
    }
    std::rename(partial.c_str(), outPath.c_str());
    std::printf("%s: %zu files, %.1f MB from %.1f MB\n", outPath.c_str(), items.size(),
                double(tocAt + tocPacked.size()) / 1e6, double(raw) / 1e6);
    return 0;
}
