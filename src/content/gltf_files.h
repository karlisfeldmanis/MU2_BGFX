#pragma once

#include <cgltf.h>

namespace mu::content {

// cgltf's options with a glTF and its buffers read through core::readFile rather than fopen, so
// the player's packs serve them as they serve every other file (core/pack.h). For
// cgltf_parse_file and cgltf_load_buffers both.
cgltf_options gltfOptions();

}  // namespace mu::content
