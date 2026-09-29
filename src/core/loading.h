// What the preloader's worker is loading, for the spinner to say (app/preloader.cpp).
//
// The loaders write and the spinner reads, on different threads, so this is the one piece of
// state the two share: a stage's name under a lock, and where the whole load has got to as
// atomics. A loader that is not behind a spinner -- a bench, a test -- writes to it and
// nothing reads it, which costs a lock and a store a stage.
//
// A stage is a share of the whole, `from` to `to` of 1, and it may say how far through it
// is with part(); one that cannot count its own work just sits at its start until the next.
//
// A loader used by more than one caller -- World::open, under both the lobby and the game --
// names its stages in its own 0 to 1, and the caller maps that onto its share with span().
#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace mu::core {

class Loading {
public:
    // Back to nothing loaded, at the start of a load.
    static void reset() {
        span(0.0f, 1.0f);
        stage("", 0.0f, 0.0f);
    }

    // The share of the whole that the stages named from here on divide between them.
    static void span(float from, float to) {
        base_().store(from);
        scale_().store(to - from);
    }

    // Enters a stage: `what` is the word the spinner shows, `from` and `to` its share of the
    // span. Written so that a read racing it can only see less done than there is, never more:
    // the spinner's number only goes up, and a glimpse of too much would stick.
    static void stage(const char* what, float from, float to) {
        {
            std::lock_guard<std::mutex> hold(lock());
            name() = what;
        }
        const float base = base_().load(), scale = scale_().load();
        within_().store(0.0f);
        to_().store(base + scale * to);
        from_().store(base + scale * from);
    }

    // How far through the current stage: `done` of `of`.
    static void part(size_t done, size_t of) {
        if (of > 0) within_().store(float(done) / float(of));
    }

    // The stage's word, and where the whole load is, 0 to 1.
    static std::string what() {
        std::lock_guard<std::mutex> hold(lock());
        return name();
    }
    static float at() {
        const float from = from_().load();
        return from + (to_().load() - from) * within_().load();
    }
    // Where the current stage ends, which the spinner creeps towards while a stage that cannot
    // count itself is under way.
    static float stageEnd() { return to_().load(); }

private:
    static std::mutex& lock() {
        static std::mutex m;
        return m;
    }
    static std::string& name() {
        static std::string s;
        return s;
    }
    static std::atomic<float>& from_() {
        static std::atomic<float> v{0.0f};
        return v;
    }
    static std::atomic<float>& to_() {
        static std::atomic<float> v{0.0f};
        return v;
    }
    static std::atomic<float>& base_() {
        static std::atomic<float> v{0.0f};
        return v;
    }
    static std::atomic<float>& scale_() {
        static std::atomic<float> v{1.0f};
        return v;
    }
    static std::atomic<float>& within_() {
        static std::atomic<float> v{0.0f};
        return v;
    }
};

}  // namespace mu::core
