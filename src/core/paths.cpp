#include "core/paths.h"

namespace mu::core {

const Folders& folders() {
    static const Folders folders = {
        MU2_ROOT_DIR,
        MU2_ASSET_DIR,
        MU2_SHEET_DIR,
        MU2_SHADER_DIR,
        MU2_FONT_DIR,
        MU2_CONTENT_DIR,
        MU2_WORK_DIR,
        MU2_USER_DIR,
        MU2_SHOT_DIR,
        MU2_CRASH_DIR,
        MU2_LOG_FILE,
        MU2_RUN_DIR,
    };
    return folders;
}

}  // namespace mu::core
