#include "game/desk.h"

#include <cstdio>

#include "core/log.h"
#include "game/play.h"

namespace mu::game {

bool Desk::open(const std::string& shaderDir, const std::string& assetDir,
                content::Textures* textures) {
    if (!interface_.init(shaderDir)) return false;
    arts_.open(assetDir, textures);
    hud_.open(interface_, &arts_);
    card_.open(interface_, &arts_);
    return true;
}

void Desk::shutdown() { interface_.shutdown(); }

void Desk::update(float seconds, const gfx::Window& window, Play& play) {
    Pointer pointer;
    window.pointer(&pointer.x, &pointer.y);
    pointer.pressed = window.clicked(0);
    pointer.released = window.released(0);
    pointer.held = window.held(0);
    pointer.rightPressed = window.clicked(1);
    if (scripted_) {
        pointer = script_;
        scripted_ = false;
    }

    panel::setScreen(float(window.height()));
    const sim::Body* hero = play.isOpen() ? &play.realm().hero() : nullptr;
    hud_.follow(hero);

    if (window.pressed(gfx::Window::Key::Inventory)) inventoryOpen_ = !inventoryOpen_;
    if (window.pressed(gfx::Window::Key::Character)) characterOpen_ = !characterOpen_;

    bool toggleInventory = false, toggleCharacter = false;
    hud_.update(seconds, float(window.width()), float(window.height()), pointer, inventoryOpen_,
                characterOpen_, &toggleInventory, &toggleCharacter);
    if (toggleInventory) inventoryOpen_ = !inventoryOpen_;
    if (toggleCharacter) characterOpen_ = !characterOpen_;

    // The character window, while it is up. What it asks for is answered here, by the realm,
    // and the window sees the answer on its next frame.
    if (characterOpen_) {
        int spend = -1;
        bool close = false;
        card_.update(float(window.width()), float(window.height()), hero, pointer, &spend,
                     &close);
        if (spend >= 0) play.spendPoint(spend);
        if (close) characterOpen_ = false;
    }

    takesPointer_ = hud_.covers(pointer.x, pointer.y) ||
                    (characterOpen_ && card_.covers(pointer.x, pointer.y));
}

void Desk::script(float x, float y, bool press, bool release) {
    scripted_ = true;
    script_ = Pointer{};
    script_.x = x;
    script_.y = y;
    script_.pressed = press;
    script_.released = release;
    script_.held = press;
    core::logf("window: scripted %s at (%.0f, %.0f)", press ? "press" : (release ? "release" : "hover"),
               double(x), double(y));
}

void Desk::submit(bgfx::ViewId view, int width, int height) {
    interface_.begin(width, height);
    interface_.add(hud_.canvas());
    if (characterOpen_) interface_.add(card_.canvas());
    interface_.submit(view);
}

std::string Desk::line() const {
    char text[160];
    std::snprintf(text, sizeof text, "windows: %u draws, %u vertices, rebuilt hud %llu card %llu",
                  interface_.draws(), interface_.vertices(),
                  (unsigned long long)hud_.rebuilds(), (unsigned long long)card_.rebuilds());
    return text;
}

}  // namespace mu::game
