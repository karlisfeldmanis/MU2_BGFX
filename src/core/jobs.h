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

}  // namespace mu::core
