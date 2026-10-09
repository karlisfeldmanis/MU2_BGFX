#include "core/jobs.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace mu::core {
namespace {

// Every core but the caller's.
int workerCount() {
    const unsigned cores = std::thread::hardware_concurrency();
    return cores > 1 ? int(cores) - 1 : 1;
}
// Below this, the hand-off costs more than it saves: run it on the caller's thread.
constexpr size_t kAlone = 8;

struct Pool {
    std::mutex lock;
    std::condition_variable wake;
    std::vector<std::thread> workers;
    // The round's work, read under the lock by a worker as it wakes.
    uint32_t round = 0;
    void (*work)(void*, size_t) = nullptr;
    void* context = nullptr;
    size_t count = 0;
    // A claim: the round in the high half, the next index in the low. A worker that wakes late
    // claims only while the round is still the one it read, so it can never run an index of
    // the next round against this one's work.
    std::atomic<uint64_t> ticket{0};
    std::atomic<size_t> finished{0};

    // Indices are taken one at a time: a pose is a few microseconds and they differ by rig.
    void drain(uint32_t r, void (*job)(void*, size_t), void* with, size_t total) {
        uint64_t t = ticket.load();
        for (;;) {
            if (uint32_t(t >> 32) != r || (t & 0xffffffffu) >= total) return;
            if (!ticket.compare_exchange_weak(t, t + 1)) continue;
            job(with, size_t(t & 0xffffffffu));
            finished.fetch_add(1, std::memory_order_release);
            t = ticket.load();
        }
    }

    void run() {
        uint32_t seen = 0;
        for (;;) {
            void (*job)(void*, size_t);
            void* with;
            size_t total;
            {
                std::unique_lock<std::mutex> hold(lock);
                wake.wait(hold, [&] { return round != seen; });
                seen = round;
                job = work;
                with = context;
                total = count;
            }
            drain(seen, job, with, total);
        }
    }
};

Pool& pool() {
    // Never destroyed: the workers wait for the process's end, which takes them with it.
    static Pool* one = [] {
        Pool* p = new Pool;
        for (int i = 0, n = workerCount(); i < n; ++i) {
            p->workers.emplace_back([p] { p->run(); });
            p->workers.back().detach();
        }
        return p;
    }();
    return *one;
}

}  // namespace

void parallelFor(size_t count, void (*work)(void* context, size_t index), void* context) {
    if (count == 0) return;
    if (count < kAlone) {
        for (size_t i = 0; i < count; ++i) work(context, i);
        return;
    }
    Pool& p = pool();
    uint32_t r;
    {
        std::lock_guard<std::mutex> hold(p.lock);
        r = ++p.round;
        p.work = work;
        p.context = context;
        p.count = count;
        p.finished.store(0);
        p.ticket.store(uint64_t(r) << 32);
    }
    p.wake.notify_all();
    p.drain(r, work, context, count);
    // Done when every index is, not when every worker has checked in: a worker that wakes after
    // the work is gone finds nothing to claim and goes back to sleep without being waited for.
    while (p.finished.load(std::memory_order_acquire) < count) std::this_thread::yield();
}

}  // namespace mu::core
