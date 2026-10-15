#!/bin/zsh
# The Windows renderer's Vulkan, checked on this Mac by Khronos' validation layer.
#
#   tools/vkcheck.sh [game switches...]     e.g. --play --click-every 90 --frames 1500 --width 960 --height 540 --cap 30
#
# Heavy: every Vulkan call checked, through MoltenVK. On 2026-10-15 a run of these back to back
# was followed by the Mac freezing hard enough to need a restart. Small window, a frame cap, one
# short run at a time, and ask the user first.
#
# Why: on 2026-10-14 two players on NVIDIA RTX cards (4060, 3070 Laptop) crashed 7-37 s into the
# world, every time, on Windows, where the game draws with Vulkan. NVIDIA's driver is strict where
# MoltenVK, under Wine on this Mac, lets a mistake pass. The validation layer names any mistake
# here, without an NVIDIA card.
#
# How: the developer's build (out/build) drawn by Vulkan (MU2_VULKAN=1, gfx/window.cpp) with the
# Windows build's SPIR-V shaders (out/build-launcher-win). bgfx opens "libMoltenVK.dylib" on the
# Mac and so would go around the Vulkan loader, which is what inserts layers: a shim folder names
# the loader libMoltenVK.dylib, and a driver file of our own points the loader at the real
# MoltenVK. Needs `brew install vulkan-loader molten-vk vulkan-validationlayers`.
#
# Out: out/vkcheck/validation.log (the layer's errors and warnings) and out/vkcheck/mu2.log.
set -e
cd "$(dirname "$0")/.."
here=$PWD
out=$here/out/vkcheck
brew=$(brew --prefix)
spirv=${MU2_SPIRV:-$here/out/build-launcher-win/shaders/spirv}
[ -f "$brew/lib/libvulkan.1.dylib" ] && [ -f "$brew/lib/libMoltenVK.dylib" ] && \
  [ -f "$brew/lib/libVkLayer_khronos_validation.dylib" ] || {
  echo "needs: brew install vulkan-loader molten-vk vulkan-validationlayers"; exit 1; }
[ -d "$spirv" ] || { echo "no SPIR-V in $spirv: build the Windows game (out/build-launcher-win) first"; exit 1; }

# The real MoltenVK under a name of its own, elsewhere: by its own name, DYLD_LIBRARY_PATH would
# hand the loader the shim -- itself -- again.
# Not called a portability driver: bgfx sets the portability flag but never enables
# VK_KHR_portability_enumeration, and the loader then hides every portability driver.
mkdir -p $out/shim $out/driver
ln -sf $brew/lib/libvulkan.1.dylib $out/shim/libMoltenVK.dylib
ln -sf $brew/lib/libMoltenVK.dylib $out/driver/libMoltenVK_driver.dylib
cat > $out/driver/moltenvk.json <<EOF
{ "file_format_version": "1.0.0",
  "ICD": { "library_path": "$out/driver/libMoltenVK_driver.dylib", "api_version": "1.4.0", "is_portability_driver": false } }
EOF
rm -f $out/validation.log

export DYLD_LIBRARY_PATH=$out/shim
# The layer's manifest names its library bare; found here, after the shim and the system.
export DYLD_FALLBACK_LIBRARY_PATH=$brew/lib:/usr/local/lib:/usr/lib
export VK_DRIVER_FILES=$out/driver/moltenvk.json
export VK_ADD_LAYER_PATH=$brew/share/vulkan/explicit_layer.d
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG
export VK_KHRONOS_VALIDATION_LOG_FILENAME=$out/validation.log
export VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn
# Every repeat kept: past the default ten, a second unbound sampler hid behind the first.
export VK_KHRONOS_VALIDATION_DUPLICATE_MESSAGE_LIMIT=100000
export MU2_VULKAN=1
export MU2_SHADER_DIR=$spirv
out/build/MU2.app/Contents/MacOS/MU2 --no-metalfx --log $out/mu2.log "$@"
