#include "game/desk.h"

#include <cstdio>

#include "core/log.h"

namespace mu::game {

bool Desk::open(const std::string& shaderDir, const std::string& assetDir,
                content::Textures* textures) {
    if (!interface_.init(shaderDir)) return false;
    arts_.open(assetDir, textures);
    hud_.open(interface_, &arts_);
    card_.open(interface_, &arts_);
    bag_.open(interface_, &arts_);
    shelf_.open(interface_, &arts_);
    interface_.adopt(ground_);
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

    // A merchant's counter opens the bag beside it and closes the character window, which is
    // MU's arrangement: the shop in column two and the inventory where it always is. Walking
    // away closes the counter in the realm, and the windows follow.
    const bool trading = play.isOpen() && play.realm().trading() >= 0;
    if (trading && !trading_) {
        characterOpen_ = false;
        bagForShop_ = !inventoryOpen_;
        inventoryOpen_ = true;
    }
    if (!trading && trading_ && bagForShop_) {
        inventoryOpen_ = false;
        bagForShop_ = false;
    }
    trading_ = trading;
    if (trading_) {
        int buy = -1;
        bool close = false;
        shelf_.update(float(window.width()), float(window.height()), 2, play.realm(), pointer,
                      shelfStage_, &buy, &close);
        if (buy >= 0) play.buy(buy);
        if (close) play.closeTrade();
    }

    // The bag, in the right-hand column or beside the character window when that is up.
    if (inventoryOpen_ && play.isOpen()) {
        BagRequests asked;
        bag_.update(float(window.width()), float(window.height()), characterOpen_ ? 2 : 1,
                    play.realm(), pointer, bagStage_, &asked);
        if (asked.moveFrom >= 0) play.moveItem(asked.moveFrom, asked.moveTo);
        if (asked.use >= 0) play.useItem(asked.use);
        // Let go outside the window. MU throws it on the ground, and there is no ground to
        // throw it on until step 7 -- so for now it stays in the bag, which is a refusal the
        // window already draws by putting the item back where it was.
        // Over the shelf it is a sale -- SendSellItemToNpcRequest -- and the realm refuses a
        // worn slot again.
        if (asked.outside >= 0 && trading_ && shelf_.covers(asked.outsideX, asked.outsideY)) {
            play.sell(asked.outside);
        } else if (asked.outside >= 0) {
            core::logf("window: %d let go outside the bag; kept", asked.outside);
        }
        if (asked.close) inventoryOpen_ = false;
    }

    if (play.isOpen()) labelGround(play, window.width(), window.height());

    takesPointer_ = hud_.covers(pointer.x, pointer.y) ||
                    (characterOpen_ && card_.covers(pointer.x, pointer.y)) ||
                    (inventoryOpen_ && (bag_.covers(pointer.x, pointer.y) || bag_.dragging())) ||
                    (trading_ && shelf_.covers(pointer.x, pointer.y));
}

void Desk::script(float x, float y, bool press, bool release, bool right) {
    scripted_ = true;
    script_ = Pointer{};
    script_.x = x;
    script_.y = y;
    script_.pressed = press && !right;
    script_.rightPressed = press && right;
    script_.released = release && !right;
    script_.held = !release && !right;
    core::logf("window: scripted %s at (%.0f, %.0f)",
               right ? "right press" : (press ? "press" : (release ? "release" : "drag")),
               double(x), double(y));
}

// MU2's Drops.Tint, which is BuildGroundItemLabelDescriptor's ladder: the colour IS the
// refinement, and Zen is gold whatever it is. The skill, luck and option rung is not reachable,
// since nothing drops with any of them.
static uint32_t tintOf(const sim::Lying& one) {
    const uint32_t yellow = gfx::rgba(1.0f, 0.8f, 0.1f);
    if (one.what.empty() || one.what.refinement >= 7) return yellow;
    const int plus = one.what.refinement;
    if (plus == 0) return gfx::rgba(0.7f, 0.7f, 0.7f);
    if (plus < 3) return gfx::rgba(0.9f, 0.9f, 0.9f);
    if (plus < 5) return gfx::rgba(1.0f, 0.5f, 0.2f);
    return gfx::rgba(0.4f, 0.7f, 1.0f);
}

void Desk::labelGround(const Play& play, int width, int height) {
    play.dropsOnScreen(viewProj_, width, height, onScreen_);
    bool same = onScreen_.size() == drawnOnScreen_.size();
    for (size_t i = 0; same && i < onScreen_.size(); ++i) {
        same = onScreen_[i].id == drawnOnScreen_[i].id && onScreen_[i].x == drawnOnScreen_[i].x &&
               onScreen_[i].y == drawnOnScreen_[i].y;
    }
    if (same && groundRebuilds_ > 0) return;
    drawnOnScreen_ = onScreen_;
    ++groundRebuilds_;
    ground_.clear();
    const content::Tables& tables = *play.realm().tables();
    const gfx::Face& face = ground_.face();
    // The tooltip's size: MU's labels are its small type, and the two read as one family.
    const float size = 8.0f * panel::scale();
    for (const Play::OnScreen& at : onScreen_) {
        const sim::Lying* one = nullptr;
        for (const sim::Lying& l : play.realm().lying()) {
            if (l.id == at.id) one = &l;
        }
        if (!one) continue;
        std::string name;
        if (one->what.empty()) {
            name = panel::commas(one->zen) + " Zen";
        } else {
            const content::ItemRow& row = tables.items[size_t(one->what.item)];
            name = one->what.refinement > 0 ? row.label + " +" + std::to_string(one->what.refinement)
                                            : row.label;
        }
        // RenderGroundItemLabelTexture: the plate is the text's own box, opaque black, and no
        // padding anywhere in it.
        const float w = face.measure(size, name), h = face.height(size);
        const gfx::Box plate{at.x - w * 0.5f, at.y - h, w, h};
        ground_.rect(plate, gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f));
        ground_.text(plate.x, plate.y + face.ascent(size), size, tintOf(*one), name);
    }
}

void Desk::submit(bgfx::ViewId view, int width, int height) {
    interface_.begin(width, height);
    interface_.add(ground_);
    interface_.add(hud_.canvas());
    if (characterOpen_) interface_.add(card_.canvas());
    if (trading_) interface_.add(shelf_.canvas());
    if (inventoryOpen_) interface_.add(bag_.canvas());
    interface_.submit(view);
}

std::string Desk::line() const {
    char text[160];
    std::snprintf(text, sizeof text, "windows: %u draws, %u vertices, rebuilt hud %llu card %llu bag %llu",
                  interface_.draws(), interface_.vertices(),
                  (unsigned long long)hud_.rebuilds(), (unsigned long long)card_.rebuilds(),
                  (unsigned long long)bag_.rebuilds());
    return text;
}

}  // namespace mu::game
