// What the windows ask for, and what the realm answers.
//
// Sprint 7's rule, and the reason every one of these returns a bool: THE INTERFACE IS A MIRROR.
// A window never changes what it shows by itself. It raises a request here, the realm decides,
// and the window redraws from the realm afterwards. The bool is the realm's answer, not this
// layer's -- nothing here may refuse on the realm's behalf.
#include "game/play.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/frustum.h"
#include "game/play_tuning.h"
#include "game/roster.h"
#include "sim/swings.h"
#include "sim/wear.h"

namespace mu::game {

uint32_t Play::spendPoint(int stat) {
    if (stat < 0 || stat > 3) return 0;
    return send({.kind = sim::Command::Kind::Spend, .a = stat});
}

void Play::castSkill(int32_t skill, uint32_t at) {
    // Aimed at what the window named, else at what he is already fighting -- which the realm
    // works out for itself, because the standing order is its own. A press on a skill he has not
    // learned, or one that is cooling, is refused down there and says nothing: the box's sweep is
    // the answer. See Realm::invoke.
    const sim::SkillRow* row = sim::skillNumbered(skill);
    // A blink is aimed at the ground under the pointer, not at a body; with no ground under it
    // there is nothing to aim at and nothing is asked.
    if (row != nullptr && row->blinks) {
        if (pointedColumn_ < 0) return;
        realm_.invokeAt(skill, pointedColumn_, pointedRow_);
        core::logf("window: %s aimed at tile %d,%d from %d,%d", row->name, pointedColumn_,
                   pointedRow_, realm_.hero().column(), realm_.hero().row());
    } else if (row != nullptr && row->aimsAtPointer() && pointedColumn_ >= 0) {
        // A skill with a direction goes the way the mouse is, body or no body
        // (SkillRow::aimsAtPointer; the user, 2026-10-02).
        realm_.invokeAt(skill, pointedColumn_, pointedRow_);
        core::logf("window: %s aimed toward tile %d,%d from %d,%d", row->name, pointedColumn_,
                   pointedRow_, realm_.hero().column(), realm_.hero().row());
    } else {
        realm_.invoke(skill, at);
    }
    core::logf("window: %s asked (cooling %lld ticks, %d mana of %d)",
               row ? row->name : "a skill", (long long)realm_.cooling(skill),
               row ? row->mana : 0, realm_.hero().mana);
}

uint32_t Play::moveItem(int from, int to) {
    return send({.kind = sim::Command::Kind::Move, .a = from, .b = to});
}

// A right-click on a carried thing. What the answer needs is taken now: the thing, where he
// stood (Go Back!'s way back, should it be a Town Portal that takes) and the swing (an Ale moves
// it, and the log says by how much).
uint32_t Play::useItem(int slot) {
    Asked asked;
    asked.item = slot >= 0 && slot < sim::kSlots ? realm_.satchel()[slot].item : -1;
    asked.column = realm_.hero().column();
    asked.row = realm_.hero().row();
    asked.facing = realm_.hero().facing;
    asked.swingMs = realm_.hero().swingMs;
    asked.swingTicks = realm_.hero().swingTicks;
    return send({.kind = sim::Command::Kind::Use, .a = slot}, asked);
}

// Two sounds, as MuMain makes them, both on the answer: ApplyJewels' SOUND_GET_ITEM01 and
// ReceiveModifyItemExtended's SOUND_JEWEL01 (WSclient.cpp:6322) -- whether the plus went up or
// down, because the client has no failure sound.
uint32_t Play::refine(int jewelSlot, int targetSlot) {
    Asked asked;
    asked.thing = targetSlot >= 0 && targetSlot < sim::kSlots ? realm_.satchel()[targetSlot] : sim::Held{};
    return send({.kind = sim::Command::Kind::Refine, .a = jewelSlot, .b = targetSlot}, asked);
}

// A drag let go over the world. The realm decides between throwing it down and opening it (a
// Firecracker or a box, Realm::cracks); the answer says which, and the noise belongs to the thing
// LANDING (Play::landed) or to the firework over its tile.
uint32_t Play::discard(int slot) {
    Asked asked;
    asked.worn = slot >= 0 && sim::wearable(slot);
    asked.item = slot >= 0 && slot < sim::kSlots ? realm_.satchel()[slot].item : -1;
    const content::ItemRow* row = asked.item >= 0 && size_t(asked.item) < tables_.items.size()
                                      ? &tables_.items[size_t(asked.item)]
                                      : nullptr;
    asked.box = row && (sim::boxOfLuck(*row) || sim::boxOfKundun(*row));
    return send({.kind = sim::Command::Kind::Discard, .a = slot}, asked);
}

// The satchel is the truth (docs/sprints/07-the-windows.md) and Realm::rearm already reads
// `hero.weapon` and `hero.shield` off it on every move that touches a worn slot; this is that
// same rule kept for the picture. Without it the figure kept whatever `Figures::dress` gave
// him at the door -- Realm::moveItem, "the satchel is the truth" -- and a weapon dragged out
// of his hand went on being drawn in it, because nothing had ever told the figure to look
// again.
std::string Play::quiverName() const {
    for (int slot : {sim::kWeaponRight, sim::kWeaponLeft}) {
        const sim::Held& held = realm_.satchel()[slot];
        if (held.empty() || size_t(held.item) >= tables_.items.size()) continue;
        const content::ItemRow& row = tables_.items[size_t(held.item)];
        if (sim::ammunition(row)) return row.name;
    }
    return {};
}

void Play::redress() {
    if (!figures_ || bare_.empty() || drawn_.empty()) return;
    const sim::Body& hero = realm_.hero();
    const std::string weapon =
        hero.weapon >= 0 ? tables_.arms[size_t(hero.weapon)].name : std::string();
    const std::string shield =
        hero.shield >= 0 ? tables_.arms[size_t(hero.shield)].name : std::string();
    // And what he wears: the five armour slots, by the asset each item row names. Without
    // these the figure only ever changed its hands, and gloves put on stayed bare hands.
    std::vector<std::string> worn;
    std::vector<ShineLook> wornShine;
    for (int slot = sim::kHelm; slot <= sim::kBoots; ++slot) {
        const sim::Held& held = realm_.satchel()[slot];
        if (held.empty() || size_t(held.item) >= tables_.items.size()) continue;
        const content::ItemRow& row = tables_.items[size_t(held.item)];
        worn.push_back(row.name);
        wornShine.push_back(shineOf(row, held.refinement, held.excellent != 0));
    }
    // How each hand's plus shows: the hand slot holding the item of that name. The realm says
    // which arm swings, not which slot it came out of.
    const auto handShine = [&](const std::string& name) {
        if (name.empty()) return ShineLook{};
        for (int slot : {sim::kWeaponRight, sim::kWeaponLeft}) {
            const sim::Held& held = realm_.satchel()[slot];
            if (!held.empty() && size_t(held.item) < tables_.items.size() &&
                tables_.items[size_t(held.item)].name == name) {
                return shineOf(tables_.items[size_t(held.item)], held.refinement,
                               held.excellent != 0);
            }
        }
        return ShineLook{};
    };
    dressedQuiver_ = quiverName();
    // The left hand's own plus: two swords of one name are two items, each with its own.
    const sim::Held& left = realm_.satchel()[sim::kWeaponLeft];
    const ShineLook leftShine =
        hero.dual && !left.empty() && size_t(left.item) < tables_.items.size()
            ? shineOf(tables_.items[size_t(left.item)], left.refinement, left.excellent != 0)
            : handShine(shield);
    // His bare body: the class's second once Sevina has taken his treasure (game/roster.h).
    const std::string bare = hero.second ? bareBody(hero.kin, true, figures_) : bare_;
    const FigureBody* look = figures_->dress(kHeroDressName, bare, weapon, shield, worn,
                                             wornShine, handShine(weapon), leftShine,
                                             dressedQuiver_);
    if (!look) return;
    Drawn& drawn = drawn_[0];
    drawn.figure.reskin(look);
    // The swing, found again exactly as Play::open finds it the first time: the stance a new
    // weapon stands him in picks a different attack clip out of the same library.
    drawn.attackClip = -1;
    if (look->library) {
        drawn.attackClip = look->library->find(attackSlotFor(look->stance));
        if (drawn.attackClip < 0) drawn.attackClip = look->library->find(38);
    }
    dualSwings(drawn, look);
}

void Play::dualSwings(Drawn& drawn, const FigureBody* look) const {
    for (int& clip : drawn.dualClips) clip = -1;
    const sim::Body& hero = realm_.hero();
    if (!hero.dual || !look || !look->library) return;
    const auto armAt = [&](int32_t at) -> const content::Arm* {
        return at >= 0 && size_t(at) < tables_.arms.size() ? &tables_.arms[size_t(at)] : nullptr;
    };
    int32_t actions[4] = {};
    if (sim::attackActions(armAt(hero.weapon), armAt(hero.shield), actions) != 4) return;
    for (int i = 0; i < 4; ++i) {
        drawn.dualClips[i] = look->library->find(actions[i]);
        if (drawn.dualClips[i] < 0) {
            for (int& clip : drawn.dualClips) clip = -1;
            return;
        }
    }
}

void Play::restore(const sim::HeroRecord& saved) {
    if (!isOpen()) return;
    realm_.restore(saved);
    redress();
}

// --give's and --lay's `+3LO2E16`: a plus, luck, an option level, and excellent options by
// OpenMU's numbers 1 to 6 (E16 is Zen and HP on armour, mana-after-kill and the excellent
// damage rate on a weapon), and `W` to put it on him rather than in the bag -- the bench's, like
// --weapon, and asked of no requirement. Anything else is ignored.
static void readExtras(const std::string& extras, int* plus, bool* luck, int* option,
                       uint8_t* excellent, bool* worn = nullptr, int* sockets = nullptr,
                       uint8_t* powers = nullptr, bool* offhand = nullptr,
                       uint8_t* affixes = nullptr) {
    int nextPower = 0, nextAffix = 0;
    for (size_t i = 0; i < extras.size(); ++i) {
        const char c = extras[i];
        // A<n>, again for each: a powered ring's or pendant's further powers (sim::Affix, 1 Wisdom
        // to 5 Fury, 6 to 9 Ice, Poison, Lightning and Fire resistance), past its own.
        // `RingWisdom::+9LA2A3A4W` is a legendary +9 worn.
        if ((c == 'A' || c == 'a') && affixes && nextAffix < 3) {
            affixes[nextAffix++] = uint8_t(std::clamp(std::atoi(extras.c_str() + i + 1), 0,
                                                      sim::kAffixes));
        }
        // S<n>: that many sockets; P<n>, again for each: the powers set in them in order, or a
        // Rune of Creation's own (sim/items.h).
        if ((c == 'S' || c == 's') && sockets) *sockets = std::atoi(extras.c_str() + i + 1);
        if ((c == 'P' || c == 'p') && powers && nextPower < 3) {
            powers[nextPower++] = uint8_t(std::atoi(extras.c_str() + i + 1));
        }
        if ((c == 'W' || c == 'w') && worn) *worn = true;
        // H: worn in the off hand -- a knight's second weapon, for a review run of the pair.
        if ((c == 'H' || c == 'h') && worn && offhand) *worn = *offhand = true;
        if (c == '+') *plus = std::atoi(extras.c_str() + i + 1);
        if (c == 'L' || c == 'l') *luck = true;
        if ((c == 'O' || c == 'o') && i + 1 < extras.size()) *option = extras[i + 1] - '0';
        if (c == 'E' || c == 'e') {
            for (size_t j = i + 1; j < extras.size() && extras[j] >= '1' && extras[j] <= '6'; ++j) {
                *excellent |= uint8_t(1u << (extras[j] - '1'));
            }
        }
    }
}

bool Play::give(const std::string& name, int count, const std::string& extras) {
    const int32_t item = tables_.itemNamed(name);
    if (item < 0) {
        core::logError("--give: no item named %s", name.c_str());
        return false;
    }
    const content::ItemRow& row = tables_.items[size_t(item)];
    const bool stacks = sim::stacks(row);
    const int durability = stacks ? std::max(1, count) : sim::fullDurability(row, 0);
    // A potion's count is poured in whole, onto his stacks and twenty a cell; anything else is
    // that many pieces, a cell each -- three jewels are three jewels, as only potions stack.
    const int pieces = stacks ? 1 : std::max(1, count);
    int slot = -1;
    for (int i = 0; i < pieces; ++i) {
        int plus = 0, option = 0;
        bool luck = false;
        uint8_t excellent = 0;
        bool worn = false;
        int sockets = 0;
        uint8_t powers[3] = {};
        uint8_t affixes[3] = {};
        bool offhand = false;
        readExtras(extras, &plus, &luck, &option, &excellent, &worn, &sockets, powers, &offhand,
                   affixes);
        const int into = offhand ? int(sim::kWeaponLeft) : worn ? sim::placeOf(row) : -1;
        // Whatever he wears there already goes into the bag first -- the arena's own sword.
        if (into >= 0 && !realm_.satchel()[into].empty()) {
            const sim::Held& on = realm_.satchel()[into];
            const content::ItemRow& onRow = tables_.items[size_t(on.item)];
            const int spare = realm_.satchel().free(tables_, onRow.width, onRow.height);
            if (spare >= 0) realm_.moveItem(into, spare);
        }
        slot = realm_.give(item, into, plus, stacks ? durability : sim::fullDurability(row, plus),
                           luck, option, excellent, uint8_t(sockets), powers, affixes);
        if (worn && slot >= 0) redress();
        core::logf("given %s%s into slot %d", row.label.c_str(),
                   stacks ? (" x" + std::to_string(durability)).c_str() : "", slot);
        if (slot < 0) break;
    }
    return slot >= 0;
}

bool Play::lay(const std::string& asked) {
    const size_t colon = asked.find(':');
    const std::string name = asked.substr(0, colon);
    int plus = 0, option = 0;
    bool luck = false;
    uint8_t excellent = 0;
    int sockets = 0;
    if (colon != std::string::npos) {
        readExtras(asked.substr(colon + 1), &plus, &luck, &option, &excellent, nullptr, &sockets);
    }
    const int32_t item = tables_.itemNamed(name);
    if (item < 0) {
        core::logError("--lay: no item named %s", name.c_str());
        return false;
    }
    const uint32_t id = realm_.lay(item, plus, luck, option, excellent, uint8_t(sockets));
    core::logf("laid %s on the ground (drop %u)", tables_.items[size_t(item)].label.c_str(), id);
    if (id == 0) return false;
    landed(id);
    return true;
}

uint32_t Play::buy(int shelfSlot) {
    return send({.kind = sim::Command::Kind::Buy, .a = shelfSlot});
}

uint32_t Play::acceptQuest(int quest) {
    return send({.kind = sim::Command::Kind::AcceptQuest, .a = quest});
}

// Go Back!'s return on this map: the realm sets him down and says Climbed, which the step draws
// as a warp's landing (Play::update), as it does a Town Portal's.
void Play::goBack(int column, int row, float facing) {
    core::logf("window: go back to %d,%d", column, row);
    send({.kind = sim::Command::Kind::GoBack, .a = column, .b = row,
          .c = int(std::lround(std::cos(facing) * 100.0f)),
          .d = int(std::lround(std::sin(facing) * 100.0f))});
}

uint32_t Play::travel(int index) {
    Asked asked;
    asked.why = int(realm_.travelRefusal(index));
    return send({.kind = sim::Command::Kind::Travel, .a = index}, asked);
}

uint32_t Play::completeQuest(int quest, int choice, sim::QuestPath path) {
    return send({.kind = sim::Command::Kind::CompleteQuest, .a = quest, .b = choice, .c = int(path)});
}

uint32_t Play::buyBack() { return send({.kind = sim::Command::Kind::BuyBack}); }

uint32_t Play::sell(int bagSlot) { return send({.kind = sim::Command::Kind::Sell, .a = bagSlot}); }

// The mending counter's two. ReceiveRepair plays SOUND_REPAIR on the reply that carries the new
// Zen, and only then, so a repair refused is the desk's no and not this sound.
uint32_t Play::repair(int slot) { return send({.kind = sim::Command::Kind::Repair, .a = slot}); }

uint32_t Play::repairAll() { return send({.kind = sim::Command::Kind::RepairAll}); }

// The vault's moves. The item ones are heard by the desk, as the bag's are (the pickup for a
// move taken, the refusal for one refused); the Zen is heard here as the coins of a sale,
// placed at the hero for the same reason.
// A vault or box move that was a jewel applied (Realm::refineAcross): its answer rung as
// Play::refine rings it, and the figure dressed again, since a worn thing may have changed.
void Play::jewelRung() {
    if (!realm_.takeJeweled()) return;
    if (const Drawn* hero = drawnOf(realm_.hero().id)) {
        emit(heard_.jewel, hero->crown[0], hero->crown[2], hero->id);
    }
    redress();
}

uint32_t Play::deposit(int bagSlot, int cell) {
    return send({.kind = sim::Command::Kind::Deposit, .a = bagSlot, .b = cell});
}

uint32_t Play::withdraw(int cell, int bagSlot) {
    return send({.kind = sim::Command::Kind::Withdraw, .a = cell, .b = bagSlot});
}

uint32_t Play::rearrange(int from, int to) {
    return send({.kind = sim::Command::Kind::Rearrange, .a = from, .b = to});
}

uint32_t Play::putIn(int bagSlot, int cell) {
    return send({.kind = sim::Command::Kind::PutIn, .a = bagSlot, .b = cell});
}

uint32_t Play::takeOut(int cell, int bagSlot) {
    return send({.kind = sim::Command::Kind::TakeOut, .a = cell, .b = bagSlot});
}

uint32_t Play::shuffle(int from, int to) {
    return send({.kind = sim::Command::Kind::Shuffle, .a = from, .b = to});
}

// The words are judged as it is asked, off the box as the player sees it when he presses OK.
uint32_t Play::mix(sim::Service service, int socket) {
    mixJudged_ = realm_.judged(service, socket);
    return send({.kind = sim::Command::Kind::Mix, .a = socket, .service = service});
}

uint32_t Play::depositZen(int64_t zen) {
    return send({.kind = sim::Command::Kind::DepositZen, .zen = zen});
}

uint32_t Play::withdrawZen(int64_t zen) {
    return send({.kind = sim::Command::Kind::WithdrawZen, .zen = zen});
}

uint32_t Play::send(sim::Command command) { return send(command, Asked()); }

uint32_t Play::send(sim::Command command, Asked asked) {
    command.player = realm_.hero().id;
    command.ticket = nextTicket_++;
    if (nextTicket_ == 0) nextTicket_ = 1;
    realm_.command(command);
    asked.command = command;
    sent_.push_back(asked);
    return command.ticket;
}

// A command's answer, in the step that applied it: what MU's client does on the server's reply
// -- the sound, the figure dressed again, the log -- and then the answer kept for the window that
// waits on its ticket (Play::answers).
void Play::answered(const sim::Happening& said) {
    using Kind = sim::Command::Kind;
    const auto at = std::find_if(sent_.begin(), sent_.end(), [&](const Asked& one) {
        return one.command.ticket == uint32_t(said.c);
    });
    if (at == sent_.end()) return;
    const Asked before = *at;
    const sim::Command& asked = before.command;
    sent_.erase(at);
    // What the realm said just before this answer, and only this answer's (see lastDrank_).
    const sim::Happening* drank = lastDrank_;
    const sim::Happening* warpedTo = lastWarped_;
    const sim::Happening* cracked = lastCracked_;
    const int levelled = levelledSince_, owedBefore = levelsBefore_;
    lastDrank_ = lastWarped_ = lastCracked_ = nullptr;
    levelledSince_ = 0;
    // A Discard that opened is answered as a Crack.
    const sim::Command::Kind kind = sim::Command::Kind(said.a);
    const bool ok = said.b >= 0;
    const long long zen = (long long)realm_.money();
    // Coins at the hero: placed, because `money_drop` is a placed event (play_open loads it that
    // way for the heap that lands on the grass) and Sound::play refuses a placed one in silence.
    const auto coins = [&](bool orTake) {
        const Drawn* hero = drawnOf(realm_.hero().id);
        if (heard_.moneyDrop >= 0 && hero && hero->placed) {
            emit(heard_.moneyDrop, hero->crown[0], hero->crown[2]);
        } else if (orTake) {
            sound_.play(heard_.take);
        }
    };
    switch (kind) {
        case Kind::Buy:
        case Kind::BuyBack:
            core::logf("window: %s %s (slot %d, %lld Zen left)",
                       asked.kind == Kind::Buy ? "buy" : "buy back", ok ? "taken" : "refused", said.b, zen);
            // ReceiveBuy's SOUND_GET_ITEM01: a purchase is a thing arriving in the bag. MU2 rang
            // coins here, which MuMain does not -- pDropMoney is only ever a heap landing.
            if (ok) sound_.play(heard_.take);
            break;
        case Kind::Sell:
            core::logf("window: sell slot %d %s (%d paid, %lld Zen now)", asked.a,
                       ok ? "taken" : "refused", said.b, zen);
            // Coins, not the pickup: a sale is Zen arriving and the thing sold LEAVING the bag.
            // MU plays ReceiveSell's SOUND_GET_ITEM01 here and this deliberately does not -- the
            // user's call, 2026-09-23.
            if (ok) coins(true);
            break;
        case Kind::Repair:
        case Kind::RepairAll:
            core::logf("window: repair %s %s (%lld Zen now)",
                       asked.kind == Kind::Repair ? "one" : "all", ok ? "taken" : "refused", zen);
            if (ok) sound_.play(heard_.repair);
            break;
        case Kind::DepositZen:
        case Kind::WithdrawZen:
            core::logf("window: vault %s %lld Zen %s (%lld carried, %lld kept)",
                       asked.kind == Kind::DepositZen ? "takes" : "gives", (long long)asked.zen,
                       ok ? "taken" : "refused", zen, (long long)realm_.vault().zen());
            if (ok) coins(false);
            break;
        case Kind::Deposit:
        case Kind::Withdraw:
        case Kind::Rearrange:
        case Kind::PutIn:
        case Kind::TakeOut:
        case Kind::Shuffle: {
            // The item moves are heard by the desk, as the bag's are; a jewel one of them applied
            // (Realm::refineAcross) is rung here, and a worn thing that left him re-dresses him.
            jewelRung();
            const int worn = asked.kind == Kind::Deposit || asked.kind == Kind::PutIn ? asked.a
                             : asked.kind == Kind::Withdraw ? asked.b
                                                            : -1;
            if (ok && worn >= 0 && sim::wearable(worn)) redress();
            if (ok && asked.kind == Kind::PutIn) mixAnswer_ = -1;
            core::logf("window: %s %d -> %d %s", 
                       asked.kind == Kind::Deposit    ? "vault deposit"
                       : asked.kind == Kind::Withdraw  ? "vault withdraw"
                       : asked.kind == Kind::Rearrange ? "vault move"
                       : asked.kind == Kind::PutIn     ? "machine takes"
                       : asked.kind == Kind::TakeOut   ? "machine gives"
                                                       : "machine move",
                       asked.a, asked.b, ok ? "taken" : "refused");
            break;
        }
        case Kind::Mix:
            if (!ok) {
                core::logf("window: mix refused (%s)", realm_.refusal().c_str());
                break;
            }
            // Heard as MU hears it: eMix, then eGem for a success and eBreak for a failure.
            mixAnswer_ = mixMade_ ? 1 : 0;
            mixWords_ = mixMade_ ? mixJudged_.success : mixJudged_.failure;
            sound_.play(heard_.mix);
            if (mixMade_) {
                if (const Drawn* hero = drawnOf(realm_.hero().id)) {
                    emit(heard_.jewel, hero->crown[0], hero->crown[2], hero->id);
                }
            } else {
                sound_.play(heard_.mixBreak);
            }
            break;
        case Kind::Spend:
            core::logf("window: a point into %s %s",
                       asked.a == 0 ? "strength" : asked.a == 1 ? "agility" : asked.a == 2 ? "vitality" : "energy",
                       ok ? "spent" : "refused, none in hand");
            break;
        case Kind::Move:
            core::logf("window: move %d -> %d %s", asked.a, asked.b, ok ? "taken" : "refused");
            if (ok) redress();
            break;
        case Kind::Use: {
            core::logf("window: use %d %s", asked.a, ok ? "taken" : "refused");
            if (!ok) break;
            if (realm_.hero().swingMs != before.swingMs) {
                core::logf("window: the swing went from %d ms (%d ticks) to %d ms (%d ticks)",
                           before.swingMs, before.swingTicks, realm_.hero().swingMs,
                           realm_.hero().swingTicks);
            }
            // The potion going down, or the apple: TryConsumeItem's own split, by what was used.
            // And the third arm, which is this project's and not MuMain's: an orb is not
            // swallowed, so the gulp is wrong on it -- `learned` throws the ribbons and the swoosh.
            const content::ItemRow* row = before.item >= 0 && size_t(before.item) < tables_.items.size()
                                              ? &tables_.items[size_t(before.item)]
                                              : nullptr;
            const bool apple = row && row->group == 14 && row->number == 0;
            if (row && row->teaches != 0) {
                learned();
            } else if (row && sim::portal(*row)) {
                portalFrom_[0] = before.column;
                portalFrom_[1] = before.row;
                portalFacing_ = before.facing;
                // Read in silence: TryConsumeItem's scroll branch sends the use and plays nothing.
                // On a map with no safe zone the realm says he is owed Lorencia (`c`), which the
                // step's own Warped branch has already taken; otherwise the arrival is drawn.
                if (!(warpedTo && warpedTo->c == 1)) warped();
            } else {
                // The Ale is a potion to TryConsumeItem, so it goes down with SOUND_DRINK01.
                sound_.play(apple ? heard_.apple : heard_.drink);
            }
            // And what it is worth, for the lane over the HUD, off the realm's own Drank.
            if (drank) (drank->b ? drankMana_ : drankHealth_) += drank->a;
            break;
        }
        case Kind::Refine: {
            const sim::Held& after = realm_.satchel()[asked.b];
            // A worn thing that outgrew him is in the bag now and its slot is empty (Realm::refine).
            const int now = !ok ? before.thing.refinement
                            : after.empty() ? before.thing.refinement + 1
                                            : after.refinement;
            core::logf("window: jewel %d on %d %s (+%d -> +%d)", asked.a, asked.b,
                       ok ? "taken" : "refused", int(before.thing.refinement), now);
            if (!ok) break;
            sound_.play(heard_.take);
            if (const Drawn* hero = drawnOf(realm_.hero().id)) {
                emit(heard_.jewel, hero->crown[0], hero->crown[2], hero->id);
            }
            // A worn thing changed rung, so the figure is dressed again at its new shine.
            if (sim::wearable(asked.b)) redress();
            break;
        }
        case Kind::Discard:
            core::logf("window: %d thrown on the ground %s", asked.a, ok ? "taken" : "refused");
            if (!ok) break;
            if (before.worn) redress();
            landed(uint32_t(said.b));
            break;
        case Kind::Crack: {
            // A Firecracker opens (sim::Realm::crack), and MU's firework starts over the tile
            // (CmdType 0; the user: "it has to be instant"). Zen goes up as a firework all the same
            // (the user, 2026-10-04). A Box of Luck or of Kundun opens with no show, as MU's do
            // (docs/drop-boxes.md section 1). What it gives waits for the show to end
            // (Play::update); an item is held out of sight from now (heldIds_).
            const uint32_t id = cracked && cracked->a >= 0 ? uint32_t(cracked->a) : 0;
            const int64_t zen = cracked && cracked->a < 0 ? int64_t(cracked->c) : 0;
            core::logf("window: %d cracked %s", asked.a, !ok ? "refused" : id ? "into an item" : "into Zen");
            if (!ok) break;
            uint32_t tag = 0;
            if (ground_ && !before.box) {
                const float metres = ground_->metresPerTile();
                const float x = (said.x + 0.5f) * metres;
                const float z = -(said.y + 0.5f) * metres;
                const float at3[3] = {x, ground_->heightAt(x, z), z};
                tag = firework_.launch(at3);
            }
            crackerOwed_.push_back({tag != 0 ? tag : ~0u, zen, id, tag != 0 ? -1.0f : 0.0f});
            if (id != 0) heldIds_.push_back(id);
            break;
        }
        case Kind::AcceptQuest:
            core::logf("window: accept quest %d %s", asked.a, ok ? "taken" : "refused");
            // The user's drum hit, the quest taken; refused, the window's own no.
            if (ok) sound_.play(sound_.load("quest_accept", false));
            else ui(Ui::Refused);
            break;
        case Kind::CompleteQuest:
            core::logf("window: hand in quest %d, choice %d, %s", asked.a, asked.b,
                       ok ? "paid" : "refused (not ready, no choice, or no room)");
            if (!ok) {
                ui(Ui::Refused);
                break;
            }
            // Each level the quest carried is its own rise, owed on no kill: the first goes up in
            // the frame the window closes, the rest after it.
            if (levelled > 0) {
                levelsOwed_ = owedBefore + levelled;
                levelOn_ = 0;
            }
            // Sevina's treasure: he is his class's second, and wears its body from this frame.
            if (sim::questAt(asked.a).promotes) {
                core::logf("quest: %s", sim::className(int(realm_.hero().kin), realm_.hero().second));
                redress();
            }
            // The user's stinger, under the "Quest complete" banner, the world leaning back for it.
            sound_.stinger("music/quest_complete.wav");
            sound_.duck();
            break;
        case Kind::Travel: {
            // By sim::TravelRefusal; `Quest` had no line here before and read past the end.
            static const char* const kWhy[] = {"",      "not opened",    "already here",
                                               "dead",  "level too low", "short of zen",
                                               "its quest not done"};
            core::logf("window: travel to %s %s", sim::travelAt(asked.a).name,
                       ok ? "paid" : kWhy[std::clamp(before.why, 0, 6)]);
            // Another map is the mode's to raise; a floor of this one the realm has already set
            // him down on, and its Climbed drew the landing (the user, 2026-10-02).
            if (ok && sim::travelAt(asked.a).map != int32_t(realm_.tables()->map)) travelled_ = asked.a;
            if (!ok) ui(Ui::Refused);
            break;
        }
        case Kind::GoBack: break;
        case Kind::None: break;
    }
    answers_.push_back({asked.ticket, kind, said.b});
}

bool Play::talkTo(const std::string& name) {
    for (size_t i = 0; i < tables_.folk.size(); ++i) {
        if (tables_.folk[i].name.find(name) == std::string::npos) continue;
        sim::Request request;
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(i);
        realm_.ask(request);
        core::logf("talk: walking to %s at (%d, %d)", tables_.folk[i].name.c_str(),
                   tables_.folk[i].x, tables_.folk[i].y);
        return true;
    }
    core::logError("--talk: nobody called %s here", name.c_str());
    return false;
}

}  // namespace mu::game
