#include "game/ui/desk.h"

#include <cstdio>

#include "core/log.h"

namespace mu::game {

// Whether a row may be bound to a key at all: CanRegisterItemHotKey's list, of which this
// catalogue has the apple, the six potions and the Town Portal Scroll. Held.Usable.
static bool usable(const content::Tables& tables, int32_t item) {
    if (item < 0) return false;
    const content::ItemRow& row = tables.items[size_t(item)];
    return sim::heals(row) || sim::restores(row) ||
           (row.group == sim::kGroupPotions && row.number == 10);
}

// Whether a carried row may stand in for a bound one: the same group, and either exactly the
// Town Portal, or the same family at no higher a rank -- the healing family falls to the
// apple and the mana family to the small mana potion. Quick.Substitutes.
static bool substitutes(const content::Tables& tables, int32_t carried, int32_t bound) {
    const content::ItemRow& c = tables.items[size_t(carried)];
    const content::ItemRow& b = tables.items[size_t(bound)];
    if (c.group != b.group) return false;
    if (b.number == 10) return c.number == 10;
    return c.number <= b.number && (b.number >= 4 ? sim::restores(c) : sim::heals(c));
}


bool Desk::open(const std::string& shaderDir, const std::string& assetDir,
                content::Textures* textures) {
    if (!interface_.init(shaderDir)) return false;
    shaderDir_ = shaderDir;
    assetDir_ = assetDir;
    textures_ = textures;
    arts_.open(assetDir, textures);
    // A stage each, so the bag and the shelf can hold different things at once, and each is
    // the window's own size: one pass draws every picture in a window and they line up with
    // its cells for free.
    bagStage_ = &bagStagePicture_;
    shelfStage_ = &shelfStagePicture_;
    hud_.open(interface_, &arts_);
    hud_.useStage(&quickStagePicture_);
    // One stage for whatever the tooltip is describing, shared: only one tip is up at a time.
    bag_.useTipStage(&tipStagePicture_);
    shelf_.useTipStage(&tipStagePicture_);
    card_.open(interface_, &arts_);
    bag_.open(interface_, &arts_);
    shelf_.open(interface_, &arts_);
    cursor_.open(interface_, &arts_);
    vitals_.open(interface_);
    arrival_.open(interface_);
    interface_.adopt(ground_);
    return true;
}

void Desk::shutdown() {
    bagStagePicture_.shutdown();
    shelfStagePicture_.shutdown();
    quickStagePicture_.shutdown();
    tipStagePicture_.shutdown();
    arrival_.shutdown();
    interface_.shutdown();
}

void Desk::update(float seconds, const gfx::Window& window, Play& play, float pointerX,
                  float pointerY) {
    // The store is handed in from outside, with the item rows already in it; until it is, the
    // windows draw each thing's name in its cell.
    Pointer pointer;
    pointer.x = pointerX;
    pointer.y = pointerY;
    pointer.pressed = window.clicked(0);
    pointer.released = window.released(0);
    pointer.held = window.held(0);
    pointer.rightPressed = window.clicked(1);
    if (scripted_) {
        pointer = script_;
        scripted_ = false;
    }

    panel::setScreen(float(window.height()));
    arrival_.update(seconds, float(window.width()), float(window.height()));
    const sim::Body* hero = play.isOpen() ? &play.realm().hero() : nullptr;
    hud_.follow(hero);

    // Every window opened or shut clicks, by key or by button: MU2's Desk.Click on each
    // Toggle, which is SOUND_CLICK01 off every button in the client.
    const auto click = [&]() {
        if (play.isOpen()) play.ui(Play::Ui::Click);
    };
    const auto refused = [&]() {
        if (play.isOpen()) play.ui(Play::Ui::Refused);
    };
    const auto took = [&]() {
        if (play.isOpen()) play.ui(Play::Ui::Took);
    };
    if (window.pressed(gfx::Window::Key::Inventory)) {
        inventoryOpen_ = !inventoryOpen_;
        click();
    }
    if (window.pressed(gfx::Window::Key::Character)) {
        characterOpen_ = !characterOpen_;
        click();
    }

    bool toggleInventory = false, toggleCharacter = false;
    hud_.update(seconds, float(window.width()), float(window.height()), pointer, inventoryOpen_,
                characterOpen_, &toggleInventory, &toggleCharacter);
    if (toggleInventory) {
        inventoryOpen_ = !inventoryOpen_;
        click();
    }
    if (toggleCharacter) {
        characterOpen_ = !characterOpen_;
        click();
    }

    // The character window, while it is up. What it asks for is answered here, by the realm,
    // and the window sees the answer on its next frame.
    if (characterOpen_) {
        int spend = -1;
        bool close = false;
        card_.update(float(window.width()), float(window.height()), hero, pointer, &spend,
                     &close);
        // The stat button clicks whether or not the point lands: CNewUICharacterInfoWindow
        // sends the request and plays SOUND_CLICK01 on the next line without waiting.
        if (spend >= 0) {
            play.spendPoint(spend);
            click();
        }
        // The exit button hides it without a sound: CNewUICharacterInfoWindow's m_BtnExit has
        // no PlayBuffer. Only Escape clicks, and the C key.
        if (close) characterOpen_ = false;
    }

    // A merchant's counter opens the bag beside it and closes the character window, which is
    // MU's arrangement: the shop in column two and the inventory where it always is. Walking
    // away closes the counter in the realm, and the windows follow.
    const bool trading = play.isOpen() && play.realm().trading() >= 0;
    // A counter opens on ReceiveTalk's click and SOUND_INTERFACE01 together. Walking away
    // shuts it silently; the shelf's own X is Escape's stand-in and clicks (below).
    if (trading && !trading_) {
        click();
        if (play.isOpen()) play.ui(Play::Ui::Opened);
    }
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
        // A purchase that goes through is heard as its coins, off the realm's Bought; one
        // refused is the interface's no.
        if (buy >= 0 && !play.buy(buy)) refused();
        if (close) {
            play.closeTrade();
            click();
        }
    }

    // The bag, in the right-hand column or beside the character window when that is up.
    if (inventoryOpen_ && play.isOpen()) {
        BagRequests asked;
        bag_.update(float(window.width()), float(window.height()), characterOpen_ ? 2 : 1,
                    play.realm(), pointer, bagStage_, &asked);
        // A move is ReceiveEquipmentItem, which ends its success branch on SOUND_GET_ITEM01 --
        // MU's equip sound is the pickup's -- and a use refused is iButtonError. A use that goes
        // through is heard as the potion going down, off the realm's Drank.
        if (asked.moveFrom >= 0) {
            if (play.moveItem(asked.moveFrom, asked.moveTo)) took();
            else refused();
        }
        if (asked.use >= 0 && !play.useItem(asked.use)) refused();
        // Let go outside the window. MU throws it on the ground, and there is no ground to
        // throw it on until step 7 -- so for now it stays in the bag, which is a refusal the
        // window already draws by putting the item back where it was.
        // Over the shelf it is a sale -- SendSellItemToNpcRequest -- and the realm refuses a
        // worn slot again.
        if (asked.outside >= 0 && trading_ && shelf_.covers(asked.outsideX, asked.outsideY)) {
            if (!play.sell(asked.outside)) refused();
        } else if (asked.outside >= 0 && hud_.quickAt(asked.outsideX, asked.outsideY) >= 0) {
            // Let go over a potion box: bound, and the thing stays in the bag. MU2's Caught.
            const int key = hud_.quickAt(asked.outsideX, asked.outsideY);
            const sim::Held& what = play.realm().satchel()[asked.outside];
            if (!what.empty() && usable(*play.realm().tables(), what.item)) {
                quick_[key] = what.item;
                core::logf("window: slot %d bound to key %d", asked.outside, key + 1);
            }
        } else if (asked.outside >= 0) {
            core::logf("window: %d let go outside the bag; kept", asked.outside);
        }
        // Silent, as CNewUIMyInventory's exit button is; the I and V keys click.
        if (asked.close) inventoryOpen_ = false;
    }

    if (play.isOpen()) {
        labelGround(play, window.width(), window.height());
        quickKeys(window, play);
        skillKeys(window, play, pointer);
    }

    takesPointer_ = hud_.covers(pointer.x, pointer.y) ||
                    (characterOpen_ && card_.covers(pointer.x, pointer.y)) ||
                    (inventoryOpen_ && (bag_.covers(pointer.x, pointer.y) || bag_.dragging())) ||
                    (trading_ && shelf_.covers(pointer.x, pointer.y));

    // The pointer, drawn last of all: MU2's Pointer.Show and Step in one call. The flags are
    // last frame's raycast (Play::point runs after this, on the same frame it is drawn), which
    // never shows -- a claw a frame behind a moving mouse is not a thing anyone can see.
    //
    // And the raycast is ignored outright where the pointer belongs to a window. Play::point
    // runs every frame whatever is open, so what lies BEHIND a window is still pointed at: a
    // pointer resting on an item in Lumen's shelf, with Lumen herself under the glass, came up
    // as the talking mouth, and over a monster it was the claw -- a cursor offering a click
    // that play_mode has already refused, since `windowed` swallows both buttons. The window's
    // own hand is the plain one, which is what MU draws over its interface.
    const bool world = play.isOpen() && !takesPointer_;
    const bool onMonster = world && play.pointedAt() != 0;
    const bool onLoot = world && play.pointedAt() == 0 && play.pointedLying() != 0;
    const bool onFolk = world && play.pointedFolk() >= 0;
    cursor_.update(seconds, pointer.x, pointer.y, onMonster, onLoot, onFolk);
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

void Desk::quickKeys(const gfx::Window& window, Play& play) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Satchel& bag = realm.satchel();
    const gfx::Window::Key keys[Hud::kQuickKeys] = {
        gfx::Window::Key::Potion1, gfx::Window::Key::Potion2, gfx::Window::Key::Potion3,
        gfx::Window::Key::Potion4, gfx::Window::Key::Potion5};
    for (int key = 0; key < Hud::kQuickKeys; ++key) {
        if (!window.pressed(keys[key]) && scriptedKey_ != key) continue;
        // Hovering a thing in the open bag and pressing the key binds it, which is MU's own
        // gesture (CNewUIMyInventory::UpdateKeyEvent); otherwise the key uses what is bound.
        const int hovered = inventoryOpen_ ? bag_.hovered() : -1;
        if (hovered >= 0 && usable(tables, bag[hovered].item)) {
            quick_[key] = bag[hovered].item;
            // SetItemHotKey plays nothing, and neither does a thing that cannot be bound.
            continue;
        }
        // A thing hovered that will not go on the bar is not used through it either: MuMain's
        // UpdateKeyEvent returns before the use when the pointer is on an item.
        if (hovered >= 0 && !bag[hovered].empty()) continue;
        if (quick_[key] < 0) continue;
        // The strongest of what may stand in for it, which is where MU's descending walk stops
        // first. Quick.Choose.
        int best = -1, strongest = -1;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            if (bag[slot].empty() || !substitutes(tables, bag[slot].item, quick_[key])) continue;
            const int number = tables.items[size_t(bag[slot].item)].number;
            if (number > strongest) {
                strongest = number;
                best = slot;
            }
        }
        if (best >= 0) play.useItem(best);
    }
    scriptedKey_ = -1;
    // And what each box shows, handed to the frame.
    for (int key = 0; key < Hud::kQuickKeys; ++key) {
        Hud::Quick q;
        q.item = quick_[key];
        if (q.item >= 0) {
            q.label = tables.items[size_t(q.item)].label;
            for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                if (!bag[slot].empty() && substitutes(tables, bag[slot].item, q.item)) {
                    q.count += std::max<int>(1, bag[slot].durability);
                }
            }
        }
        hud_.setQuick(key, q);
    }
}

// The skill bar: the four keys, and what the four boxes show.
//
// The press is the whole of the gesture and the realm decides everything about it -- learned,
// cooling, in reach, paid for. What this owes the player is the PICTURE of that decision, which is
// why the box is handed the cooldown as a fraction and in seconds rather than a bool: a skill that
// says nothing while it cools is a key the player thinks is broken.
void Desk::skillKeys(const gfx::Window& window, Play& play, const Pointer& pointer) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();

    // Bound on the day it is learned, first free key first. A convenience while there is one
    // skill and no list to drag from; it binds nothing a second time, so a rebinding by hand
    // would stick.
    for (int i = 0; i < sim::skillCount(); ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        if (!realm.knows(row.number)) continue;
        bool already = false;
        for (int key = 0; key < Hud::kSkillKeys; ++key) already |= bound_[key] == row.number;
        if (already) continue;
        for (int key = 0; key < Hud::kSkillKeys; ++key) {
            if (bound_[key] != 0) continue;
            bound_[key] = row.number;
            core::logf("window: %s bound to %s", row.name,
                       key == 0 ? "Q" : key == 1 ? "W" : key == 2 ? "E" : "R");
            break;
        }
    }

    const gfx::Window::Key keys[Hud::kSkillKeys] = {
        gfx::Window::Key::Skill1, gfx::Window::Key::Skill2, gfx::Window::Key::Skill3,
        gfx::Window::Key::Skill4};
    for (int key = 0; key < Hud::kSkillKeys; ++key) {
        if (!window.pressed(keys[key]) && scriptedSkill_ != key) continue;
        if (bound_[key] == 0) continue;
        // Aimed at what the pointer is over when it is over something, else at nothing -- the
        // realm falls back to whatever the standing order is fighting, which is the usual case:
        // the knight is already swinging at it.
        play.castSkill(bound_[key], play.pointedAt());
    }
    scriptedSkill_ = -1;

    // The card, for the one box the pointer is resting on. Built here and not in the frame,
    // because every number on it is the realm's -- and built for one box, because four cards a
    // frame is four sheets of strings nobody reads.
    const int over = hud_.skillAt(pointer.x, pointer.y);

    for (int key = 0; key < Hud::kSkillKeys; ++key) {
        Hud::Skill box;
        box.number = bound_[key];
        if (box.number != 0) {
            box.icon = "skill_" + std::to_string(box.number);
            const int64_t left = realm.cooling(box.number);
            const int32_t whole = realm.coolsFor(box.number);
            box.cooling = whole > 0 ? float(left) / float(whole) : 0.0f;
            box.seconds = float(left) * 0.05f;  // 20 Hz
            const sim::SkillRow* row = sim::skillNumbered(box.number);
            // Dimmed for either reason he cannot throw it: the mana is not there, or there is no
            // blade in his hand (Realm::throwSkill refuses both). MU dims a hotkey it will not
            // honour and says nothing else, and the plate decides nothing here -- it asks the
            // same two questions the realm will ask.
            const content::Arm* weapon =
                hero.weapon >= 0 && size_t(hero.weapon) < tables.arms.size()
                    ? &tables.arms[size_t(hero.weapon)]
                    : nullptr;
            const content::Arm* shield =
                hero.shield >= 0 && size_t(hero.shield) < tables.arms.size()
                    ? &tables.arms[size_t(hero.shield)]
                    : nullptr;
            // A blade for an attack, a shield for the guard -- the two hands the realm asks
            // about, asked here in the same order so the icon dims for the same reason.
            const bool armed = row != nullptr && row->onSelf()
                                   ? shield != nullptr && shield->isShield()
                                   : weapon != nullptr && !weapon->isShield() &&
                                         !weapon->bow() && !weapon->crossbow();
            box.affordable = (row == nullptr || hero.mana >= row->mana) && armed;

            if (key == over && row != nullptr) {
                hud_.setSkillSheet(key, skillSheet(*row, realm, armed));
            }
        }
        hud_.setSkill(key, box);
    }
}

// What one skill's card says. The order is the order a player asks the questions in: what is
// this, what does it do, what does it hit for, what does it cost me, and -- if the key is dark --
// why. Every number is read off the realm and off `sim/skills.h`'s own formulas, so the card and
// the blow can never disagree: `force()` is what the damage multiplies by and `coolsFor()` is
// what the cooldown will be set to, the same calls `Realm::throwSkill` makes.
tip::Sheet Desk::skillSheet(const sim::SkillRow& row, const sim::Realm& realm, bool armed) const {
    const sim::Body& hero = realm.hero();
    tip::Sheet sheet;
    sheet.name = row.name;
    sheet.nameTone = tip::Tone::Blue;
    // Narrower than an item's card. An item wraps lore and a column of options; a skill has one
    // sentence and three numbers, and the item's width left most of the card empty.
    sheet.wide = 232.0f;

    const auto number = [](float value, int places) {
        char text[32];
        std::snprintf(text, sizeof(text), places == 1 ? "%.1f" : "%.2f", double(value));
        return std::string(text);
    };
    const auto line = [](const std::string& label, const std::string& value, tip::Tone tone) {
        tip::Row one;
        one.label = label;
        one.values.push_back({value, tone, false, "", 0});
        return one;
    };

    // What it does, in the row's own line.
    if (row.tells[0] != '\0') {
        tip::Section what;
        tip::Row prose;
        prose.free = row.tells;
        prose.freeTone = tip::Tone::Gray;
        what.rows.push_back(prose);
        sheet.sections.push_back(what);
    }

    // And three numbers, which is the whole of the card.
    //
    // **Only the essentials, on the user's word of 2026-09-23.** What went: the class line, the
    // reach (always his own), the section headings and their marks, and both derivations -- the
    // "x2.00 base, +0.08 from strength" and the "4.0 s base, -6% from agility". The derivations
    // were the teaching bit and they are the first thing to go all the same: what a player acts on
    // is the multiplier his blow HAS and the wait he actually faces, and both of those already
    // carry the stat inside them. The formulas live in docs/skills-dk.md, where they are read
    // once, rather than on a card read fifty times a fight.
    tip::Section facts;
    const int64_t left = realm.cooling(row.number);
    const float seconds = float(realm.coolsFor(row.number)) * 0.05f;
    if (row.onSelf()) {
        facts.rows.push_back(line("Damage taken", "x" + number(row.damageTaken, 2) + " for " +
                                                      number(float(row.boonTicks) * 0.05f, 1) + " s",
                                  tip::Tone::Green));
    } else {
        facts.rows.push_back(line("Damage", "x" + number(sim::force(row, hero.points), 2) +
                                                " of a swing",
                                  tip::Tone::Yellow));
        // And the sum that made it, on one grey line: the row's own base plus strength over the
        // skill's divisor. Asked for on 2026-09-23 -- the multiplier says what he hits for and
        // this says WHY, which is the whole argument for spending on strength, and it is one
        // line rather than the two labelled rows it was before.
        char sum[64];
        std::snprintf(sum, sizeof(sum), "%.2f + %d str / %d", double(row.force),
                      hero.points.strength,
                      row.forcePerStrength > 0.0f ? int(1.0f / row.forcePerStrength + 0.5f) : 0);
        tip::Row how;
        how.free = sum;
        how.freeTone = tip::Tone::Gray;
        facts.rows.push_back(how);
    }
    if (left > 0) {
        facts.rows.push_back(line("Ready in", number(float(left) * 0.05f, 1) + " s",
                                  tip::Tone::Red));
    } else {
        facts.rows.push_back(line("Cooldown", number(seconds, 1) + " s", tip::Tone::White));
    }
    const bool paid = hero.mana >= row.mana;
    facts.rows.push_back(line("Mana", std::to_string(row.mana), paid ? tip::Tone::Blue
                                                                     : tip::Tone::Red));
    sheet.sections.push_back(facts);

    // The one refusal the numbers do not already show: an empty hand. A mana shortfall is the red
    // figure above it and needs no sentence.
    if (!armed) {
        tip::Section why;
        tip::Row need;
        need.free = row.onSelf() ? "Needs a shield on his arm." : "Needs a blade in his hand.";
        need.freeTone = tip::Tone::Red;
        why.rows.push_back(need);
        sheet.sections.push_back(why);
    }
    return sheet;
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

void Desk::overhead(float seconds, const Play& play, const float* viewProj, int width,
                    int height) {
    if (!play.isOpen()) {
        vitals_.dismiss();
        return;
    }
    vitals_.update(seconds, play, play.pointedAt(), takesPointer_, viewProj, width, height);
}

void Desk::photograph(gfx::Renderer& renderer, double seconds) {
    // The pictures are drawn at the scale the windows are drawn at, so nothing is resampled.
    if (!models_ || !models_->tables() || !renderer.openStages(shaderDir_)) return;
    const float pixelsPerUnit = panel::scale();
    if (inventoryOpen_) bagStagePicture_.render(renderer, pixelsPerUnit, seconds);
    if (trading_) shelfStagePicture_.render(renderer, pixelsPerUnit, seconds);
    // The potion boxes are always on screen, and at rest their stage costs nothing.
    quickStagePicture_.render(renderer, hud_.pixelsPerUnit(), seconds);
    // The tooltip's picture, at the windows' own scale: nothing stands on it unless a tip is up.
    tipStagePicture_.render(renderer, panel::scale(), seconds);
}

void Desk::submit(bgfx::ViewId view, int width, int height) {
    interface_.begin(width, height);
    interface_.add(ground_);
    // Over the world's labels and under every window: it is a reading lying on the scene.
    if (vitals_.showing()) interface_.add(vitals_.canvas());
    // The map's name, a reading on the scene as well, and under every window.
    if (arrival_.showing()) interface_.add(arrival_.canvas());
    interface_.add(hud_.canvas());
    if (characterOpen_) interface_.add(card_.canvas());
    if (trading_) interface_.add(shelf_.canvas());
    if (inventoryOpen_) interface_.add(bag_.canvas());
    // The tips over every window, and under the pointer. Whichever is hovered, it is the one
    // thing on the panel the player is reading at that moment.
    interface_.add(hud_.tipCanvas());
    if (trading_) interface_.add(shelf_.tipCanvas());
    if (inventoryOpen_) interface_.add(bag_.tipCanvas());
    // Last of all, over every window too: MU2's own CanvasLayer{Layer=128} -- a pointer is over
    // whatever it is pointing at, and the panel is something you point at as well.
    interface_.add(cursor_.canvas());
    interface_.submit(view);
}

std::string Desk::line() const {
    char text[160];
    std::snprintf(text, sizeof text, "windows: %u draws, %u vertices, rebuilt hud %llu card %llu bag %llu vitals %llu",
                  interface_.draws(), interface_.vertices(),
                  (unsigned long long)hud_.rebuilds(), (unsigned long long)card_.rebuilds(),
                  (unsigned long long)bag_.rebuilds(),
                  (unsigned long long)vitals_.rebuilds());
    return text;
}

}  // namespace mu::game
