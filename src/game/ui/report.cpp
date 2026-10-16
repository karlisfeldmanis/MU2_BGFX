#include "game/ui/report.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// In tip::unit() at 1080 lines: the head, the question, the well of words, what goes with it, a
// line kept for the answer so the window does not jump when one comes, and the buttons right.
constexpr float kWide = 480.0f;
constexpr float kInset = 22.0f;
constexpr float kAskTop = style::kHead + 16.0f, kAskTall = 22.0f;
constexpr float kWellTop = kAskTop + kAskTall + 8.0f, kWellTall = 168.0f;
constexpr float kWellPad = 12.0f;
constexpr float kWithTop = kWellTop + kWellTall + 14.0f, kWithTall = 20.0f;
constexpr float kAnswerTop = kWithTop + kWithTall * 2.0f + 6.0f, kAnswerTall = 22.0f;
constexpr float kButtonTop = kAnswerTop + kAnswerTall + 8.0f;
constexpr float kButtonW = 120.0f;
constexpr float kTall = kButtonTop + style::kButtonM + 20.0f;
constexpr float kText = 15.0f;   // the words' size
constexpr float kSmall = 13.0f;  // what goes with it, the count
// The site keeps 2000 (web/server.py); a player's paragraph is well under it.
constexpr size_t kMost = 1000;
constexpr int kMostLines = 12;  // new lines typed, so the well is never overrun by Enter alone
constexpr float kBlink = 0.5f;
constexpr float kThanksSeconds = 2.5f;
constexpr const char* kAsk = "What went wrong? Say what you did and what you saw.";
constexpr const char* kPlaceholder = "For example: I sold a sword to Hanzo and it stayed in my bag.";

Box buttonBox(int which) {
    if (which == 2) {
        const float side = style::kSmallSquare;
        return {kWide - style::kPad - side, (style::kHead - side) * 0.5f, side, side};
    }
    const float x = which == 0 ? kWide - kInset - kButtonW : kWide - kInset - kButtonW * 2.0f - style::kGap;
    return {x, kButtonTop, kButtonW, style::kButtonM};
}

Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

// The bytes of the UTF-8 character that ends `s`.
size_t lastCharacter(const std::string& s) {
    size_t n = 1;
    while (n < s.size() && (uint8_t(s[s.size() - n]) & 0xC0) == 0x80) ++n;
    return std::min(n, s.size());
}

// The words broken into lines no wider than `wide` pixels: at the typed new lines, then at the
// spaces, and a word too long for a line at its letters.
std::vector<std::string> wrap(const std::string& text, float wide, float size) {
    std::vector<std::string> lines;
    size_t from = 0;
    while (from <= text.size()) {
        const size_t end = std::min(text.find('\n', from), text.size());
        const std::string para = text.substr(from, end - from);
        std::string line;
        size_t at = 0;
        while (at < para.size()) {
            const size_t space = std::min(para.find(' ', at), para.size());
            std::string word = para.substr(at, space - at);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (controls::labelWidth(size, tried) <= wide) {
                line = tried;
            } else {
                if (!line.empty()) lines.push_back(line);
                line.clear();
                // Longer than a line on its own: cut it where it stops fitting.
                while (controls::labelWidth(size, word) > wide && word.size() > 1) {
                    size_t cut = word.size();
                    while (cut > 1 && controls::labelWidth(size, word.substr(0, cut)) > wide) --cut;
                    lines.push_back(word.substr(0, cut));
                    word = word.substr(cut);
                }
                line = word;
            }
            at = space + 1;
        }
        // A space typed last stays, so the caret moves on as it is typed.
        if (!para.empty() && para.back() == ' ') line += ' ';
        lines.push_back(line);
        if (end >= text.size()) break;
        from = end + 1;
    }
    return lines;
}

}  // namespace

void Report::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

void Report::show(const std::string& about) {
    up_ = true;
    state_ = State::Writing;
    text_.clear();
    why_.clear();
    about_ = about;
    clock_ = sentFor_ = 0.0f;
    over_ = pressing_ = -1;
    lift_[0] = lift_[1] = lift_[2] = 0.0f;
}

void Report::answered(bool sent, const std::string& why) {
    if (!up_ || state_ != State::Sending) return;
    state_ = sent ? State::Sent : State::Refused;
    why_ = sent ? std::string() : why;
    sentFor_ = 0.0f;
}

int Report::buttonAt(float ux, float uy) const {
    for (int which = 0; which < 3; ++which) {
        if (buttonBox(which).has(ux, uy)) return which;
    }
    return -1;
}

void Report::update(float seconds, float width, float height, const Pointer& pointer, const std::string& typed,
                    int backspaces, bool enter, bool escape, Result* out) {
    Result result;
    if (up_) {
        const float k = tip::unit();
        x_ = std::round((width - kWide * k) * 0.5f);
        y_ = std::round(height * 0.42f - kTall * k * 0.5f);
        clock_ += seconds;

        // Words while it is being written, and while a refusal stands, which an edit takes down.
        const bool editable = state_ == State::Writing || state_ == State::Refused;
        if (editable) {
            const size_t before = text_.size();
            const int lines = int(std::count(text_.begin(), text_.end(), '\n'));
            for (char c : typed) {
                if (uint8_t(c) < 0x20 || text_.size() >= kMost) continue;
                text_ += c;
            }
            if (enter && lines < kMostLines && text_.size() < kMost && !text_.empty()) text_ += '\n';
            for (int i = 0; i < backspaces && !text_.empty(); ++i) text_.resize(text_.size() - lastCharacter(text_));
            if (text_.size() != before || backspaces > 0) {
                clock_ = 0.0f;
                if (state_ == State::Refused) {
                    state_ = State::Writing;
                    why_.clear();
                }
            }
        }

        const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
        over_ = buttonAt(ux, uy);
        for (int which = 0; which < 3; ++which) {
            const float step = seconds / style::kHoverSeconds;
            lift_[which] = over_ == which ? std::min(1.0f, lift_[which] + step) : std::max(0.0f, lift_[which] - step);
        }
        if (pointer.pressed) pressing_ = over_;
        int fired = -1;
        if (pointer.released) {
            if (pressing_ >= 0 && pressing_ == over_) fired = pressing_;
            pressing_ = -1;
        }
        // Words with something in them besides spaces and new lines.
        const bool said = text_.find_first_not_of(" \n") != std::string::npos;
        if (escape || fired == 1 || fired == 2) {
            up_ = false;
            result.cancel = result.clicked = true;
        } else if (fired == 0 && editable && said) {
            state_ = State::Sending;
            result.send = result.clicked = true;
        }
        if (state_ == State::Sent) {
            sentFor_ += seconds;
            if (sentFor_ >= kThanksSeconds) up_ = false;
        }
    }
    if (out) *out = result;

    now_ = Drawn{};
    now_.up = up_;
    if (up_) {
        now_.state = state_;
        now_.text = text_;
        now_.about = about_;
        now_.why = why_;
        now_.caret = (state_ == State::Writing || state_ == State::Refused) && std::fmod(clock_, kBlink * 2.0f) < kBlink;
        now_.x = x_;
        now_.y = y_;
        now_.scale = tip::unit();
        now_.over = over_;
        now_.pressing = pressing_;
        for (int which = 0; which < 3; ++which) now_.lift[which] = lift_[which];
    }
    if (built_ && now_ == drawn_) return;
    drawn_ = now_;
    built_ = true;
    rebuild();
}

void Report::rebuild() {
    canvas_.clear();
    if (!up_) return;
    const float x = x_, y = y_, u = tip::unit();
    const float inset = kInset * u;

    canvas_.rect({0.0f, 0.0f, 1e5f, 1e5f}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.25f));
    controls::frame(canvas_, placed(x, y, {0.0f, 0.0f, kWide, kTall}, u), u, "Report a Bug");
    const auto state = [&](int which, bool off) {
        const float t = lift_[which] * lift_[which] * (3.0f - 2.0f * lift_[which]);
        return controls::State{t, pressing_ == which && over_ == which, off};
    };
    controls::square(canvas_, placed(x, y, buttonBox(2), u), controls::Glyph::Close, state(2, false), u);

    const Box ask = placed(x, y, {0.0f, kAskTop, kWide, kAskTall}, u);
    controls::label(canvas_, ask.x + inset, controls::middle(ask.y, ask.h, style::kBodySize * u),
                    style::kBodySize * u, style::kBone2, kAsk);

    // The well, and the words wrapped in it from the top; the lines that no longer fit scroll
    // off its top, so the line being typed is always the one seen.
    const Box well = placed(x, y, {kInset, kWellTop, kWide - kInset * 2.0f, kWellTall}, u);
    controls::well(canvas_, well, u);
    const float size = kText * u, pad = kWellPad * u, step = std::round(size * 1.35f);
    const float inner = well.w - pad * 2.0f;
    const int fit = std::max(1, int((well.h - pad * 2.0f) / step));
    if (text_.empty()) {
        const std::vector<std::string> hint = wrap(kPlaceholder, inner, size);
        for (size_t i = 0; i < hint.size() && int(i) < fit; ++i) {
            controls::label(canvas_, well.x + pad, well.y + pad + step * float(i) + size * 0.95f, size,
                            style::kAshInk2, hint[i]);
        }
        if (now_.caret) canvas_.rect({well.x + pad, well.y + pad + 2.0f * u, std::max(1.0f, 2.0f * u), size * 1.1f},
                                     style::kBloodHi);
    } else {
        const std::vector<std::string> lines = wrap(text_, inner, size);
        const size_t first = lines.size() > size_t(fit) ? lines.size() - size_t(fit) : 0;
        float lastRight = well.x + pad, lastTop = well.y + pad;
        for (size_t i = first; i < lines.size(); ++i) {
            const float top = well.y + pad + step * float(i - first);
            controls::label(canvas_, well.x + pad, top + size * 0.95f, size, style::kBone, lines[i]);
            lastRight = well.x + pad + controls::labelWidth(size, lines[i]);
            lastTop = top;
        }
        if (now_.caret) {
            canvas_.rect({std::round(lastRight + 1.0f * u), lastTop + 2.0f * u, std::max(1.0f, 2.0f * u), size * 1.1f},
                         style::kBloodHi);
        }
    }
    // How much room is left, small in the well's corner, once it is getting close.
    if (text_.size() > kMost * 3 / 4) {
        const std::string left = std::to_string(kMost - std::min(kMost, text_.size())) + " left";
        controls::ranged(canvas_, well.x + well.w - pad, well.y + well.h - pad * 0.6f, kSmall * u, style::kAshInk,
                         left);
    }

    // What goes with it, so he knows what he is sending.
    const Box with = placed(x, y, {0.0f, kWithTop, kWide, kWithTall}, u);
    controls::caps(canvas_, with.x + inset, controls::middle(with.y, with.h, style::kKickerSize * u),
                   style::kKickerSize * u, style::kAshInk, "Sent with it", style::kKickerTrack);
    controls::label(canvas_, with.x + inset, controls::middle(with.y + with.h, with.h, kSmall * u), kSmall * u,
                    style::kBone2, about_ + "  \xC2\xB7  your game version and graphics card");

    // The answer's line.
    const Box answer = placed(x, y, {0.0f, kAnswerTop, kWide, kAnswerTall}, u);
    const float base = controls::middle(answer.y, answer.h, 14.0f * u);
    if (state_ == State::Sending) {
        controls::label(canvas_, answer.x + inset, base, 14.0f * u, style::kAshInk, "Sending...");
    } else if (state_ == State::Sent) {
        controls::label(canvas_, answer.x + inset, base, 14.0f * u, style::kLive, "Sent. Thank you!");
    } else if (state_ == State::Refused) {
        controls::message(canvas_, answer.x + inset, base, why_.empty() ? "Not sent." : why_, u);
    }

    const bool said = text_.find_first_not_of(" \n") != std::string::npos;
    const bool sendable = said && (state_ == State::Writing || state_ == State::Refused);
    controls::button(canvas_, placed(x, y, buttonBox(1), u), state_ == State::Sent ? "Close" : "Cancel",
                     controls::Kind::Secondary, state(1, false), u);
    controls::button(canvas_, placed(x, y, buttonBox(0), u), "Send", controls::Kind::Primary, state(0, !sendable),
                     u);
}

}  // namespace mu::game
