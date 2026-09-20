// The sim's one source of chance, pinned.
//
// Pinned is the whole point. .NET's `Random` -- which OpenMU and MU2's Realm draw from -- is
// not reproducible across runtimes and so cannot be the reference; neither can <random>'s
// distributions, whose bit consumption is the standard library's business and not the
// standard's. So the generator is ours, written out here, and the same seed gives the same
// bytes on any build of this engine.
//
// What must be kept, because the sim's log is compared byte for byte:
//
//   * `nextInt(min, max)` is UPPER-EXCLUSIVE, as OpenMU's `Rand.NextInt` is.
//   * `nextBool(chance)` is `<=`, as `Rand.cs:63-71` is. MU2's own reading made it `<`, which
//     is immaterial in floating point and is still a departure it did not mark.
//   * A draw not taken is as load-bearing as one taken. The critical roll happens only when
//     there is a chance to roll against and the damage roll only when max > min; hoisting
//     either out of its condition consumes a number and shifts every later draw in the run.
//
// xoshiro256** over a splitmix64 seeding, both public domain and both written out in full
// rather than referenced, because "the usual one" is not a specification.
#pragma once

#include <cstdint>

namespace mu::sim {

class Random {
public:
    explicit Random(uint64_t seed = 0) { this->seed(seed); }

    void seed(uint64_t value) {
        // splitmix64, which is what xoshiro's own author seeds it with: a bad state (all
        // zeroes, which seed 0 would otherwise give) is a generator that returns zero forever.
        for (uint64_t& part : state_) {
            value += 0x9e3779b97f4a7c15ull;
            uint64_t z = value;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            part = z ^ (z >> 31);
        }
        draws_ = 0;
    }

    uint64_t next() {
        ++draws_;
        const uint64_t result = rotate(state_[1] * 5, 7) * 9;
        const uint64_t t = state_[1] << 17;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotate(state_[3], 45);
        return result;
    }

    // [0, 1). The 53 bits a double holds, taken from the top, which is where this generator's
    // bits are best.
    double nextDouble() { return double(next() >> 11) * 0x1.0p-53; }

    // [min, max), as OpenMU's Rand.NextInt is. `max <= min` gives min and draws nothing --
    // which is also OpenMU's behaviour at the one place it matters, the damage roll.
    int nextInt(int min, int max) {
        if (max <= min) return min;
        // Modulo, and the bias is written down rather than hidden: over a span of at most a
        // few hundred it is about one part in 2^56, which is smaller than the difference
        // between any two things this decides. The alternative -- rejection -- would consume a
        // variable number of draws, and a variable number of draws is the one thing this class
        // must not have.
        return min + int(next() % uint64_t(uint32_t(max - min)));
    }

    // `Rand.NextRandomBool`: true when the draw is at or below the chance. Note `<=`.
    bool nextBool(double chance) {
        if (chance <= 0.0) return false;
        return nextDouble() <= chance;
    }

    // How many numbers have been drawn since the seed. Not used by any rule -- it is in the
    // log, because "the run diverged at draw 41 207" is the only cheap way to find out where
    // two supposedly identical runs stopped agreeing.
    uint64_t draws() const { return draws_; }

private:
    static uint64_t rotate(uint64_t x, int bits) { return (x << bits) | (x >> (64 - bits)); }

    uint64_t state_[4] = {};
    uint64_t draws_ = 0;
};

}  // namespace mu::sim
