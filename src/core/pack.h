#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::core {

// The player's packs (docs/plans/layout.md, "What a player is given"). A .mupak holds files under
// logical paths -- "assets/cooked/lorencia/lorencia.mur", "sheets/lighting.json",
// "fonts/Cinzel-Bold.ttf" -- each zstd-compressed, behind a table of contents. The player's build
// mounts every pack in its data folder at start; from then core::readFile, fileExists and
// listFiles answer a path under that folder from the packs, and the game never knows. The
// developer's build mounts nothing and reads its loose files as it always did.
//
// The file, all integers little-endian:
//
//   header   "MUPK", u32 version, u32 entries, u32 tocPacked, u32 tocSize, u32 0, u64 tocAt
//   data     each entry's bytes, compressed or stored, scrambled
//   toc      at tocAt: zstd of the rows, scrambled, tocPacked bytes and tocSize when opened.
//            A row: u16 path length, the path, u64 at, u32 packed, u32 size, u8 method
//
// Scrambled is an XOR with a keystream seeded by where the bytes sit. It is not encryption and
// does not pretend to be: it keeps a pack from opening as a zstd stream or a list of names in
// any tool that is not ours. What protects the game is the server (docs/plans/layout.md).
constexpr uint32_t kPackMagic = 0x4b50554d;  // "MUPK"
constexpr uint32_t kPackVersion = 1;
constexpr uint8_t kPackStored = 0;
constexpr uint8_t kPackZstd = 1;

// Scrambles or unscrambles `n` bytes in place; `at` is where the first of them sits in the
// pack, so a slice read on its own unscrambles the same as the whole.
void packScramble(uint8_t* bytes, size_t n, uint64_t at);

// Mounts every *.mupak in `dir`, their paths taken as under `dir`. Returns the packs mounted.
int mountPacks(const std::string& dir);
bool packsMounted();

// A path's whole file from the packs. False if no pack holds it, and then `out` is untouched.
bool packRead(const std::string& path, std::vector<uint8_t>& out);
bool packHas(const std::string& path);
// The names of the files directly in `dir` that the packs hold, as directory_iterator would
// give their leaves.
std::vector<std::string> packList(const std::string& dir);

}  // namespace mu::core
