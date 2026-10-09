#pragma once

// What a net bot says on the chat (net::Say, net::Heard), beside what its brain does. Ours: MU's
// bots never spoke, and these are players to whoever shares the server with them.
//
// A bot says little, and only what a player would:
//
//   * his own moments, on his map: a level of ten, a quest handed in, now and then a death;
//   * an answer to a whisper from a player, a second or three later, once in a while per sender;
//   * and, to another's line, a short answer: "gz" to a level, "yes, me" to an "anyone here?".
//
// Two bots never talk forever: a bot answers only an OPENING line (opens()), and no answer is
// one, so every exchange is a line and its answers. And he never answers a bot's whisper.
//
// The party's lines -- asking for one, accepting -- are the party's (docs/party.md): it says them
// through say(), as anything else may.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "net/wire.h"
#include "sim/quests.h"
#include "sim/realm.h"

namespace mu::bot {

class Talk {
public:
    // Ticks of the realm's 20 Hz clock, which is what `now` counts in throughout.
    static constexpr int64_t kSecond = 20;

    explicit Talk(std::string name) : name_(std::move(name)) {
        uint32_t seed = 2166136261u;
        for (const char c : name_) seed = (seed ^ uint8_t(c)) * 16777619u;
        dice_.seed(seed);
    }

    // `--chatty`: his own lines every half minute or so, for watching the chat work.
    void setChatty(bool on) { chatty_ = on; }

    // A line to say `delay` ticks from now: the party's, or anyone's.
    void say(net::Channel channel, std::string text, int64_t now, int64_t delay = 0,
             std::string to = {}) {
        queued_.push_back({now + delay, {channel, std::move(to), std::move(text)}});
    }

    // What his own world said of him this tick: his moments.
    void happened(const sim::Realm& mirror, uint32_t you, int64_t now) {
        for (const sim::Happening& h : mirror.happenings()) {
            if (h.who != you) continue;
            if (h.what == sim::What::Levelled && h.a % 10 == 0 && chance(0.6)) {
                mine(now, pick({"level " + std::to_string(h.a), "ding " + std::to_string(h.a),
                                 std::to_string(h.a) + " finally"}));
            } else if (h.what == sim::What::QuestDone && h.a >= 0 && h.a < sim::kQuests && chance(0.35)) {
                mine(now, std::string("done with ") + sim::questAt(h.a).title);
            } else if (h.what == sim::What::Died && chance(0.2)) {
                mine(now, pick({"died again", "that hurt", "ugh, back to town"}));
            }
        }
    }

    // A line heard on the chat.
    void heard(const net::Heard& line, int64_t now) {
        if (line.name.empty() || line.name == name_) return;  // the server's, or his own come back
        if (line.channel == net::Channel::Whisper) {
            if (line.bot || !line.to.empty()) return;
            whispered(line, now);
            return;
        }
        if (line.channel != net::Channel::Map || !opens(line.text) || !chance(0.5)) return;
        if (now - answeredAt_ < (chatty_ ? 5 : 30) * kSecond) return;
        answeredAt_ = now;
        const std::string answer = answerTo(line.text);
        if (!answer.empty()) say(net::Channel::Map, answer, now, (2 + roll(5)) * kSecond);
    }

    // And, now and then, a line of his own to his map: one in about twenty minutes.
    void idle(int64_t now) {
        const int64_t minute = chatty_ ? kSecond : 60 * kSecond;  // chatty: seconds for minutes
        if (nextIdle_ == 0) nextIdle_ = now + (10 + roll(20)) * minute;
        if (now < nextIdle_) return;
        nextIdle_ = now + (15 + roll(15)) * minute;
        std::string line;
        do {  // never his last one again
            line = pick({"anyone else hunting here?", "quiet here today", "anyone seen a Golden Dragon?",
                         "this map drops nothing"});
        } while (line == lastIdle_);
        lastIdle_ = line;
        mine(now, line);
    }

    // The lines due by now, oldest first, into `into`.
    void take(int64_t now, std::vector<net::Say>& into) {
        while (!queued_.empty() && queued_.front().due <= now) {
            into.push_back(std::move(queued_.front().said));
            queued_.pop_front();
        }
    }

private:
    struct Due {
        int64_t due = 0;
        net::Say said;
    };

    // His own line to his map, at most one in two minutes.
    void mine(int64_t now, std::string text) {
        if (now - saidAt_ < (chatty_ ? 10 : 120) * kSecond && saidAt_ != 0) return;
        saidAt_ = now;
        say(net::Channel::Map, std::move(text), now, (1 + roll(3)) * kSecond);
    }

    void whispered(const net::Heard& line, int64_t now) {
        int64_t& last = whisperedAt_[line.name];
        if (last != 0 && now - last < 20 * kSecond) return;
        last = now;
        const std::string t = lower(line.text);
        std::string answer;
        if (has(t, {"hi", "hello", "hey", "yo", "sup"})) {
            answer = pick({"hey", "hi " + line.name, "hello"});
        } else if (has(t, {"party", "pt", "inv", "group"})) {
            answer = pick({"maybe later, finishing a quest", "not now, sorry"});
        } else if (has(t, {"sell", "buy", "trade", "zen"})) {
            answer = "not trading, sorry";
        } else if (has(t, {"bot"})) {
            answer = pick({"beep", "who, me?"});
        } else {
            answer = pick({"busy hunting, sorry", "can't talk, mid fight", "sorry, afk soon"});
        }
        say(net::Channel::Whisper, answer, now, (1 + roll(3)) * kSecond, line.name);
    }

    // An opening line: one that asks to be answered. No answer of answerTo is one.
    static bool opens(const std::string& text) {
        const std::string t = lower(text);
        return has(t, {"level", "ding"}) || t.find("anyone") != std::string::npos;
    }

    std::string answerTo(const std::string& text) {
        const std::string t = lower(text);
        if (has(t, {"level", "ding"})) return pick({"gz", "grats", "gz!"});
        if (t.find("hunting") != std::string::npos) return pick({"yes, me", "me, on the east side"});
        if (t.find("dragon") != std::string::npos) return pick({"not today", "no, sadly"});
        return {};
    }

    static std::string lower(std::string s) {
        for (char& c : s) c = char(std::tolower(uint8_t(c)));
        return s;
    }
    // Whether `t` holds any of `words` as a whole word.
    static bool has(const std::string& t, std::initializer_list<const char*> words) {
        for (const char* w : words) {
            const std::string word = w;
            for (size_t at = t.find(word); at != std::string::npos; at = t.find(word, at + 1)) {
                const bool before = at == 0 || !std::isalnum(uint8_t(t[at - 1]));
                const size_t end = at + word.size();
                const bool after = end >= t.size() || !std::isalnum(uint8_t(t[end]));
                if (before && after) return true;
            }
        }
        return false;
    }

    bool chance(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(dice_) < p; }
    int64_t roll(int n) { return std::uniform_int_distribution<int>(0, n - 1)(dice_); }
    std::string pick(std::initializer_list<std::string> lines) {
        return *(lines.begin() + roll(int(lines.size())));
    }

    std::string name_;
    std::mt19937 dice_;  // his own, never the realm's: the chat is outside the lockstep
    std::deque<Due> queued_;
    std::map<std::string, int64_t> whisperedAt_;
    std::string lastIdle_;
    int64_t saidAt_ = 0, answeredAt_ = 0, nextIdle_ = 0;
    bool chatty_ = false;
};

}  // namespace mu::bot
