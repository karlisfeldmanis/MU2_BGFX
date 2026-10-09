#include "core/jobs.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <pthread/qos.h>
#include <sys/sysctl.h>
#endif

namespace mu::core {
namespace {

// Every performance core but the caller's. A worker on an efficiency core, or one more than the
// fast cores hold, is a worker the kernel parks mid-item while the frame waits on that item: in
// the Lost Tower's fight the frame's thread posed six samples of the wings and waited 107.
int workerCount() {
    int fast = 0;
#if defined(__APPLE__)
    size_t size = sizeof(fast);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &fast, &size, nullptr, 0) != 0) fast = 0;
#endif
    const int cores = fast > 0 ? fast : int(std::thread::hardware_concurrency());
    return cores > 1 ? cores - 1 : 1;
}
// Below this, the hand-off costs more than it saves: run it on the caller's thread.
constexpr size_t kAlone = 8;

struct Pool {
    std::mutex lock;
    std::condition_variable wake;
    std::vector<std::thread> workers;
    // The round's work, read under the lock by a worker as it wakes.
    std::atomic<uint32_t> round{0};
    // Workers asleep on `wake`, counted under the lock: a round with none asleep wakes nobody.
    int sleeping = 0;
    void (*work)(void*, size_t) = nullptr;
    void* context = nullptr;
    size_t count = 0;
    // A claim: the round in the high half, the next index in the low. A worker that wakes late
    // claims only while the round is still the one it read, so it can never run an index of
    // the next round against this one's work.
    std::atomic<uint64_t> ticket{0};
    std::atomic<size_t> finished{0};
    // An index's own "done", for parallelForInOrder's caller to take it by. Grown by the caller
    // between rounds only, when no worker holds an index.
    std::unique_ptr<std::atomic<uint8_t>[]> done;
    size_t doneSize = 0;

    // One index, if the round has one left: false when it has not.
    bool claim(uint32_t r, void (*job)(void*, size_t), void* with, size_t total,
               std::atomic<uint8_t>* flags) {
        uint64_t t = ticket.load();
        for (;;) {
            if (uint32_t(t >> 32) != r || (t & 0xffffffffu) >= total) return false;
            if (ticket.compare_exchange_weak(t, t + 1)) break;
        }
        const size_t index = size_t(t & 0xffffffffu);
        job(with, index);
        flags[index].store(1, std::memory_order_release);
        finished.fetch_add(1, std::memory_order_release);
        return true;
    }

    // Indices are taken one at a time: a pose is a few microseconds and they differ by rig.
    void drain(uint32_t r, void (*job)(void*, size_t), void* with, size_t total,
               std::atomic<uint8_t>* flags) {
        while (claim(r, job, with, total, flags)) {
        }
    }

    void run() {
#if defined(__APPLE__)
        // The frame's own class, as the main thread's: a worker at the default class is put on
        // an efficiency core or behind the machine's other work while it holds a pose the frame
        // is waiting for.
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
        uint32_t seen = 0;
        for (;;) {
            void (*job)(void*, size_t);
            void* with;
            size_t total;
            std::atomic<uint8_t>* flags;
            {
                std::unique_lock<std::mutex> hold(lock);
                if (round.load() == seen) {
                    ++sleeping;
                    wake.wait(hold, [&] { return round.load() != seen; });
                    --sleeping;
                }
                seen = round.load();
                job = work;
                with = context;
                total = count;
                flags = done.get();
            }
            drain(seen, job, with, total, flags);
        }
    }

    // A new round of `total`, its workers woken; the caller then drains or takes.
    uint32_t start(void (*job)(void*, size_t), void* with, size_t total) {
        uint32_t r;
        bool asleep;
        {
            std::lock_guard<std::mutex> hold(lock);
            if (doneSize < total) {
                doneSize = total + total / 2;
                done.reset(new std::atomic<uint8_t>[doneSize]);
            }
            for (size_t i = 0; i < total; ++i) done[i].store(0, std::memory_order_relaxed);
            r = round.load() + 1;
            work = job;
            context = with;
            count = total;
            finished.store(0);
            ticket.store(uint64_t(r) << 32);
            round.store(r, std::memory_order_release);
            asleep = sleeping > 0;
        }
        if (asleep) wake.notify_all();
        return r;
    }

    // Done when every index is, not when every worker has checked in: a worker that wakes after
    // the work is gone finds nothing to claim and goes back to sleep without being waited for.
    void finish(size_t total) {
        while (finished.load(std::memory_order_acquire) < total) std::this_thread::yield();
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
    const uint32_t r = p.start(work, context, count);
    p.drain(r, work, context, count, p.done.get());
    p.finish(count);
}

void parallelForInOrder(size_t count, void (*work)(void* context, size_t index),
                        void (*take)(void* context, size_t index), void* context) {
    if (count == 0) return;
    if (count < kAlone) {
        for (size_t i = 0; i < count; ++i) work(context, i);
        for (size_t i = 0; i < count; ++i) take(context, i);
        return;
    }
    Pool& p = pool();
    const uint32_t r = p.start(work, context, count);
    std::atomic<uint8_t>* flags = p.done.get();
    for (size_t i = 0; i < count; ++i) {
        // Its own work while the one it takes next is still out, and a yield only once there is
        // none left to claim.
        while (flags[i].load(std::memory_order_acquire) == 0) {
            if (!p.claim(r, work, context, count, flags)) std::this_thread::yield();
        }
        take(context, i);
    }
    // Every flag is set, but a worker's count of it may land a moment after: the next round
    // zeroes the count, and must not have this one's land on top.
    p.finish(count);
}

}  // namespace mu::core
