// miniaudio's file system over the player's packs (game/sound.h, packSoundFiles).
//
// A sound's whole file is read through core::readFile and served from memory, so the packs serve
// it as they serve every other file (core/pack.h). Given only while packs are mounted: the
// developer's build streams its music from the disk as it always did.
#include "game/sound.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include <miniaudio.h>

#include "core/files.h"
#include "core/pack.h"

namespace mu::game {
namespace {

struct HeldFile {
    std::vector<uint8_t> bytes;
    size_t at = 0;
};

ma_result heldOpen(ma_vfs*, const char* path, ma_uint32 mode, ma_vfs_file* file) {
    if ((mode & MA_OPEN_MODE_WRITE) != 0) return MA_ACCESS_DENIED;
    auto held = std::make_unique<HeldFile>();
    held->bytes = core::readFile(path);
    if (held->bytes.empty()) return MA_DOES_NOT_EXIST;
    *file = held.release();
    return MA_SUCCESS;
}
ma_result heldOpenW(ma_vfs*, const wchar_t*, ma_uint32, ma_vfs_file*) { return MA_NOT_IMPLEMENTED; }
ma_result heldClose(ma_vfs*, ma_vfs_file file) {
    delete static_cast<HeldFile*>(file);
    return MA_SUCCESS;
}
ma_result heldRead(ma_vfs*, ma_vfs_file file, void* out, size_t size, size_t* read) {
    HeldFile& held = *static_cast<HeldFile*>(file);
    const size_t n = std::min(size, held.bytes.size() - held.at);
    std::memcpy(out, held.bytes.data() + held.at, n);
    held.at += n;
    if (read) *read = n;
    return n == 0 && size > 0 ? MA_AT_END : MA_SUCCESS;
}
ma_result heldWrite(ma_vfs*, ma_vfs_file, const void*, size_t, size_t*) { return MA_ACCESS_DENIED; }
ma_result heldSeek(ma_vfs*, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin) {
    HeldFile& held = *static_cast<HeldFile*>(file);
    const ma_int64 base = origin == ma_seek_origin_start     ? 0
                          : origin == ma_seek_origin_current ? ma_int64(held.at)
                                                             : ma_int64(held.bytes.size());
    const ma_int64 to = base + offset;
    if (to < 0 || to > ma_int64(held.bytes.size())) return MA_BAD_SEEK;
    held.at = size_t(to);
    return MA_SUCCESS;
}
ma_result heldTell(ma_vfs*, ma_vfs_file file, ma_int64* cursor) {
    *cursor = ma_int64(static_cast<HeldFile*>(file)->at);
    return MA_SUCCESS;
}
ma_result heldInfo(ma_vfs*, ma_vfs_file file, ma_file_info* info) {
    info->sizeInBytes = static_cast<HeldFile*>(file)->bytes.size();
    return MA_SUCCESS;
}

ma_vfs_callbacks g_held = {heldOpen,  heldOpenW, heldClose, heldRead,
                           heldWrite, heldSeek,  heldTell,  heldInfo};

}  // namespace

void* packSoundFiles() { return core::packsMounted() ? &g_held : nullptr; }

}  // namespace mu::game
