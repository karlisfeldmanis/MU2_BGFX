// A small pool for work a frame can split: parallelFor runs `work(i)` for every i below `count`
// on the calling thread and the pool's workers together, and returns when all are done.
//
// A worker a core but one, made on first use and kept; the frame's own thread is the last hand.
// The user, 2026-10-09: "we need to use mutiple cores that the point of smooth gameplay". The
// work must touch only what its index owns -- a figure's pose, its own slot of scratch.
#pragma once

#include <cstddef>

namespace mu::core {

void parallelFor(size_t count, void (*work)(void* context, size_t index), void* context);

// The same with a lambda, which is called as work(index).
template <typename Work>
void parallelFor(size_t count, Work&& work) {
    parallelFor(count, [](void* context, size_t index) { (*static_cast<Work*>(context))(index); },
                &work);
}

// work(i) on every core as parallelFor's, and take(i) on the calling thread in index order, each
// as soon as its own work(i) is done -- the caller lending a hand with the work while the next
// one it takes is still out. What a frame does in order with each result (a pose's palette and
// draws) then runs beside the rest of the work rather than after the slowest item of it.
void parallelForInOrder(size_t count, void (*work)(void* context, size_t index),
                        void (*take)(void* context, size_t index), void* context);

template <typename Work, typename Take>
void parallelForInOrder(size_t count, Work&& work, Take&& take) {
    struct Both {
        Work& work;
        Take& take;
    } both{work, take};
    parallelForInOrder(
        count, [](void* context, size_t index) { static_cast<Both*>(context)->work(index); },
        [](void* context, size_t index) { static_cast<Both*>(context)->take(index); }, &both);
}

}  // namespace mu::core
