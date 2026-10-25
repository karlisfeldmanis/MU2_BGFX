#include "app/modes/director.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/log.h"

namespace mu::app {

namespace {

int skillKey(const std::string& word) {
    static const char kKeys[] = "qwert";
    if (word.size() == 1) {
        if (const char* at = std::strchr(kKeys, word[0])) return int(at - kKeys);
        if (word[0] >= '1' && word[0] <= '5') return word[0] - '1';
    }
    return -1;
}

// `run`: how far ahead of him the pointer rides, in tiles, and how often it clicks there.
constexpr float kAhead = 5.0f;
constexpr double kRunClicks = 0.45;

// A hand's ease: slow off, slow in, as a person moves a mouse.
float ease(float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
}

}  // namespace

bool Director::open(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        core::logError("director: cannot read %s", path.c_str());
        return false;
    }
    std::string raw;
    int number = 0;
    while (std::getline(in, raw)) {
        ++number;
        const size_t hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        std::istringstream words(raw);
        std::string first;
        if (!(words >> first)) continue;
        if (first == "args" || first == "end") continue;  // record.py's
        Cue cue;
        cue.line = number;
        char* tail = nullptr;
        cue.at = std::strtod(first.c_str(), &tail);
        std::string verb;
        if (*tail != '\0' || !(words >> verb)) {
            core::logError("director: %s:%d: a cue is SECONDS VERB ...", path.c_str(), number);
            return false;
        }
        bool fine = true;
        cue.seconds = 0.5f;
        if (verb == "move") {
            cue.verb = Verb::Move;
            fine = bool(words >> cue.a >> cue.b);
            if (fine && !(words >> cue.seconds)) cue.seconds = 0.0f;
        } else if (verb == "aim") {
            std::string what;
            words >> what;
            if (what == "monster") cue.verb = Verb::AimMonster;
            else if (what == "elite") cue.verb = Verb::AimElite;
            else if (what == "loot") cue.verb = Verb::AimLoot;
            else if (what == "hero") cue.verb = Verb::AimHero;
            else if (what == "tile") {
                cue.verb = Verb::AimTile;
                fine = bool(words >> cue.a >> cue.b);
            } else fine = false;
            float s = 0.0f;
            if (fine && words >> s) cue.seconds = s;
        } else if (verb == "click" || verb == "rclick") {
            cue.verb = verb == "click" ? Verb::Click : Verb::RightClick;
        } else if (verb == "hold" || verb == "rhold") {
            cue.verb = verb == "hold" ? Verb::Hold : Verb::RightHold;
            fine = bool(words >> cue.seconds);
        } else if (verb == "skill" || verb == "hold-skill") {
            std::string key;
            words >> key;
            cue.key = skillKey(key);
            cue.verb = verb == "skill" ? Verb::Skill : Verb::HoldSkill;
            fine = cue.key >= 0 && (cue.verb == Verb::Skill || bool(words >> cue.seconds));
        } else if (verb == "key" || verb == "shift-key") {
            cue.verb = Verb::Potion;
            cue.shifted = verb == "shift-key";
            fine = bool(words >> cue.key) && cue.key >= 1 && cue.key <= 4;
            cue.key -= 1;
        } else if (verb == "run") {
            cue.verb = Verb::Run;
            fine = bool(words >> cue.a >> cue.b);
        } else if (verb == "stop") {
            cue.verb = Verb::Stop;
        } else if (verb == "wear") {
            cue.verb = Verb::Wear;
            for (std::string one; words >> one;) cue.words.push_back(one);
            fine = !cue.words.empty();
        } else if (verb == "unwear") {
            cue.verb = Verb::Unwear;
            fine = bool(words >> cue.key);
        } else if (verb == "flare") {
            cue.verb = Verb::Flare;
        } else if (verb == "pin") {
            cue.verb = Verb::Pin;
            for (float& one : cue.pins) fine = fine && bool(words >> one);
        } else if (verb == "type") {
            cue.verb = Verb::Type;
            std::getline(words >> std::ws, cue.text);
            fine = !cue.text.empty();
        } else {
            fine = false;
        }
        if (!fine) {
            core::logError("director: %s:%d: cannot read '%s'", path.c_str(), number, raw.c_str());
            return false;
        }
        cues_.push_back(cue);
    }
    std::stable_sort(cues_.begin(), cues_.end(),
                     [](const Cue& l, const Cue& r) { return l.at < r.at; });
    on_ = true;
    core::logf("director: %zu cues from %s; the mouse is not read", cues_.size(), path.c_str());
    return true;
}

bool Director::target(const Eyes& eyes, float width, float height, float* x, float* y) const {
    if (aim_ == Aim::Screen) {
        *x = aimA_ * width;
        *y = aimB_ * height;
        return true;
    }
    float column = aimA_, row = aimB_;
    if (aim_ == Aim::Body && !eyes.shownAt(aimBody_, &column, &row)) return false;
    if (aim_ == Aim::Ahead) {
        if (!eyes.shownAt(eyes.hero, &column, &row)) return false;
        const float length = std::sqrt(aimA_ * aimA_ + aimB_ * aimB_);
        if (length <= 0.0f) return false;
        column += aimA_ / length * kAhead;
        row += aimB_ / length * kAhead;
    }
    // The middle of the tile, and for a body a little up it, on its chest rather than its feet.
    if (!eyes.toScreen(column + 0.5f, row + 0.5f, x, y)) return false;
    if (aim_ == Aim::Body) *y -= height * 0.04f;
    return true;
}

Director::Hand Director::step(double seconds, float width, float height, const Eyes& eyes) {
    Hand hand;
    if (x_ < 0.0f) {
        x_ = width * 0.5f;
        y_ = height * 0.5f;
    }
    bool leftNow = false, rightNow = false;
    // A cue at a frame's own second fires on that frame, though the clock summed frame by frame
    // may have fallen a hair short of it.
    while (next_ < cues_.size() && cues_[next_].at <= seconds + 1e-4) {
        const Cue& cue = cues_[next_++];
        const auto glide = [&](Aim aim, float a, float b, uint32_t body) {
            fromX_ = x_;
            fromY_ = y_;
            glideAt_ = seconds;
            glideFor_ = cue.seconds;
            aim_ = aim;
            aimA_ = a;
            aimB_ = b;
            aimBody_ = body;
        };
        switch (cue.verb) {
            case Verb::Move: glide(Aim::Screen, cue.a, cue.b, 0); break;
            case Verb::AimMonster: {
                const uint32_t id = eyes.nearestMonster();
                if (id) glide(Aim::Body, 0, 0, id);
                else core::logf("director: line %d: no monster to aim at", cue.line);
                break;
            }
            case Verb::AimElite: {
                const uint32_t id = eyes.nearestElite ? eyes.nearestElite() : 0;
                if (id) glide(Aim::Body, 0, 0, id);
                else core::logf("director: line %d: no Elite to aim at", cue.line);
                break;
            }
            case Verb::AimHero: glide(Aim::Body, 0, 0, eyes.hero); break;
            case Verb::AimLoot: {
                float column = 0, row = 0;
                if (eyes.nearestLoot(&column, &row)) glide(Aim::Tile, column, row, 0);
                else core::logf("director: line %d: nothing lying to aim at", cue.line);
                break;
            }
            case Verb::AimTile: glide(Aim::Tile, cue.a, cue.b, 0); break;
            case Verb::Click: leftNow = true; leftUntil_ = seconds; break;
            case Verb::RightClick: rightNow = true; rightUntil_ = seconds; break;
            case Verb::Hold: leftNow = true; leftUntil_ = seconds + cue.seconds; break;
            case Verb::RightHold: rightNow = true; rightUntil_ = seconds + cue.seconds; break;
            case Verb::Skill: hand.skill = cue.key; break;
            case Verb::HoldSkill:
                hand.skill = cue.key;
                heldSkill_ = cue.key;
                skillUntil_ = seconds + cue.seconds;
                break;
            case Verb::Potion: hand.potion = cue.key, hand.shifted = cue.shifted; break;
            case Verb::Type: hand.typed = cue.text; break;
            case Verb::Run:
                glide(Aim::Ahead, cue.a, cue.b, 0);
                glideFor_ = 0.4f;
                running_ = true;
                clickAt_ = seconds;
                break;
            case Verb::Stop: running_ = false; break;
            case Verb::Wear: hand.wear = cue.words; break;
            case Verb::Unwear: hand.unwear = cue.key; break;
            case Verb::Flare: hand.flare = true; break;
            case Verb::Pin:
                hand.pin = true;
                hand.pinClip = int(cue.pins[0]);
                hand.pinHero = cue.pins[1];
                hand.pinBeat = cue.pins[2];
                hand.pinPulse = cue.pins[3];
                hand.pinGallop = cue.pins[4];
                break;
        }
    }

    // Running: a click every so often on the tile ahead, as a player keeps him going.
    // It walks him to the tile under the pointer and no further: a monster that has wandered under
    // it is not struck, which would stop him and break the run.
    float column = 0.0f, row = 0.0f;
    if (running_ && seconds + 1e-4 >= clickAt_ && eyes.shownAt(eyes.hero, &column, &row)) {
        const float length = std::sqrt(aimA_ * aimA_ + aimB_ * aimB_);
        hand.walk = length > 0.0f;
        hand.walkColumn = int(std::lround(column + aimA_ / std::max(length, 1e-6f) * kAhead));
        hand.walkRow = int(std::lround(row + aimB_ / std::max(length, 1e-6f) * kAhead));
        clickAt_ = seconds + kRunClicks;
    }

    // The pointer: along the glide toward wherever the aim is now, then kept on it.
    float toX = x_, toY = y_;
    if (target(eyes, width, height, &toX, &toY)) {
        const float u = glideFor_ > 0.0f ? float((seconds - glideAt_) / glideFor_) : 1.0f;
        const float e = ease(u);
        x_ = fromX_ + (toX - fromX_) * e;
        y_ = fromY_ + (toY - fromY_) * e;
    }
    hand.x = x_;
    hand.y = y_;

    // The buttons: down on the cue's frame, up on the first frame past the hold.
    if (leftNow) {
        hand.pressed = true;
        leftDown_ = true;
    } else if (leftDown_ && seconds > leftUntil_) {
        hand.released = true;
        leftDown_ = false;
    }
    hand.held = leftDown_;
    if (rightNow) {
        hand.rightPressed = true;
        rightDown_ = true;
    } else if (rightDown_ && seconds > rightUntil_) {
        rightDown_ = false;
    }
    hand.rightHeld = rightDown_;
    if (heldSkill_ >= 0) {
        if (seconds <= skillUntil_) hand.holdSkill = heldSkill_;
        else heldSkill_ = -1;
    }
    return hand;
}

}  // namespace mu::app
