// rules.json's `look`: numbers the site draws the way the game draws, from the game's headers
// (src/game/play_tuning.h's Elite gold). Header-only constants, so nothing of the renderer links.
#include "dump.h"
#include "game/play_tuning.h"

namespace armory {

J lookJson() {
    J light = J::arr(), chrome = J::arr();
    for (float v : mu::game::kEliteLight) light.push(J(v));
    for (float v : mu::game::kEliteChrome) chrome.push(J(v));
    return J::obj().set("kEliteLight", light).set("kEliteChrome", chrome);
}

}  // namespace armory
