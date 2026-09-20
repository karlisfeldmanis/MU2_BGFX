#include "gfx/stats.h"

#include <algorithm>
#include <cstdio>

#include <bgfx/bgfx.h>

#include "core/log.h"

namespace mu::gfx {
namespace {

// The value at a quantile of a copy of the samples. Not an interpolation: with a few
// hundred frames the nearest rank is the honest answer.
double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    size_t k = size_t(q * double(v.size() - 1) + 0.5);
    std::nth_element(v.begin(), v.begin() + long(k), v.end());
    return v[k];
}

}  // namespace

void Stats::begin(const std::string& csvPath, const std::vector<core::BudgetOverride>& overrides) {
    overrides_ = overrides;
    frames_.reserve(4096);
    if (csvPath.empty()) return;
    csv_ = std::fopen(csvPath.c_str(), "w");
    if (!csv_) {
        core::logError("cannot write stats to %s", csvPath.c_str());
        return;
    }
    std::fprintf(csv_, "frame,cpu_ms,gpu_ms,draws");
    for (uint16_t v = 0; v < ViewCount; ++v) std::fprintf(csv_, ",%s_ms", viewName(View(v)));
    std::fprintf(csv_, "\n");
    core::logf("stats to %s", csvPath.c_str());
}

void Stats::sample(double cpuMs) {
    const bgfx::Stats* s = bgfx::getStats();
    Frame f;
    f.cpuMs = cpuMs;
    f.draws = s->numDraw;

    const double toMs = 1000.0 / double(s->gpuTimerFreq);
    f.gpuMs = double(s->gpuTimeEnd - s->gpuTimeBegin) * toMs;

    // Per-view times come from the profiler; they are only populated with BGFX_RESET_PROFILER
    // on, and a view that submitted nothing is absent rather than zero.
    for (uint32_t i = 0; i < s->numViews; ++i) {
        const bgfx::ViewStats& vs = s->viewStats[i];
        if (vs.view < ViewCount) f.viewMs[vs.view] = double(vs.gpuTimeEnd - vs.gpuTimeBegin) * toMs;
    }

    if (csv_) {
        std::fprintf(csv_, "%zu,%.4f,%.4f,%u", frames_.size(), f.cpuMs, f.gpuMs, f.draws);
        for (uint16_t v = 0; v < ViewCount; ++v) std::fprintf(csv_, ",%.4f", f.viewMs[v]);
        std::fprintf(csv_, "\n");
    }
    frames_.push_back(f);
}

double Stats::allowance(Account a) const {
    for (const auto& o : overrides_) {
        if (o.account == accountName(a)) return o.ms;
    }
    return accountBudgetMs(a);
}

bool Stats::finish(bool enforce) {
    if (csv_) {
        std::fclose(csv_);
        csv_ = nullptr;
    }
    if (frames_.size() <= kWarmup) {
        core::logf("no summary: %zu frames, and the first %zu are warmup", frames_.size(), kWarmup);
        // A run with nothing left after the warmup cannot be judged -- and a gate that
        // cannot judge must not report that it did. `--frames 10 --budget gpu=0.001` used to
        // exit 0 here, which turned "ask for a short run" into a way of switching the gate
        // off; the shortest runs are exactly the ones a caller reaches for when a number is
        // inconvenient. Asking for the gate and giving it nothing to measure is now the
        // caller's error, and it fails the run. Not asking for it is still fine: a bench run
        // of a handful of frames is a perfectly good thing to do.
        if (enforce) {
            core::logError("--budget was asked for on %zu frames, of which %zu are warmup; "
                           "there is nothing to judge",
                           frames_.size(), kWarmup);
            return false;
        }
        return true;
    }
    const size_t first = kWarmup;
    const size_t n = frames_.size() - first;

    std::vector<double> cpu, gpu, fps;
    cpu.reserve(n);
    gpu.reserve(n);
    fps.reserve(n);
    std::vector<double> perAccount[AccountCount];
    for (auto& v : perAccount) v.reserve(n);

    for (size_t i = first; i < frames_.size(); ++i) {
        const Frame& f = frames_[i];
        cpu.push_back(f.cpuMs);
        gpu.push_back(f.gpuMs);
        if (f.cpuMs > 0.0) fps.push_back(1000.0 / f.cpuMs);
        double acc[AccountCount] = {};
        for (uint16_t v = 0; v < ViewCount; ++v) acc[viewAccount(View(v))] += f.viewMs[v];
        for (int a = 0; a < AccountCount; ++a) perAccount[a].push_back(acc[a]);
    }

    const double gpuMed = quantile(gpu, 0.5);
    const double cpuMed = quantile(cpu, 0.5);

    // Whether the per-view timers add up to the frame they are supposed to divide. On Metal
    // each view is its own render pass encoder and the timestamps are taken at its edges, so
    // the gaps between encoders -- and the wait for the drawable, which lands on whichever
    // view presents -- are counted inside the views. Measured on the House01 bench: five
    // accounts summing to 9.4 ms inside a frame whose whole GPU time was 1.6 ms. So the
    // accounts are reported and the *frame* is what is enforced.
    double medianSum = 0.0;
    for (int a = 0; a < AccountCount; ++a) medianSum += quantile(perAccount[a], 0.5);
    const bool viewsAddUp = medianSum <= gpuMed * 1.25 + 0.05;

    core::logf("--- %zu frames measured, %zu warmup dropped ---", n, first);
    core::logf("%-10s %8s %8s %8s %8s", "account", "median", "p99", "share", "budget");
    for (int a = 0; a < AccountCount; ++a) {
        const double med = quantile(perAccount[a], 0.5);
        const double p99 = quantile(perAccount[a], 0.99);
        // The documented allowance, or a claim made on the command line for this run.
        const double budget = allowance(Account(a));
        // The share is what this view would cost if the timers' proportions are right and
        // their total is not. It is the number to read while `viewsAddUp` is false.
        const double share = medianSum > 0.0 ? med / medianSum * gpuMed : 0.0;
        const bool over = share > budget;
        core::logf("%-10s %8.3f %8.3f %8.3f %8.3f%s", accountName(Account(a)), med, p99, share,
                   budget, over ? "  over" : "");
    }
    if (!viewsAddUp) {
        core::logf("the view timers sum to %.3f ms inside a %.3f ms frame, so they are "
                   "encoder gaps as much as work: read the share column, not the median",
                   medianSum, gpuMed);
    }

    const double gpuBudget = totalGpuBudgetMs();
    const bool gpuOver = gpuMed > gpuBudget;
    const bool cpuOver = cpuMed > cpuBudgetMs();
    core::logf("%-10s %8.3f %8.3f %8s %8.3f%s", "gpu frame", gpuMed, quantile(gpu, 0.99), "",
               gpuBudget, gpuOver ? "  OVERDRAWN" : "");
    core::logf("%-10s %8.3f %8.3f %8s %8.3f%s", "cpu", cpuMed, quantile(cpu, 0.99), "",
               cpuBudgetMs(), cpuOver ? "  OVERDRAWN" : "");
    core::logf("%-10s %8.1f %8.1f", "fps", quantile(fps, 0.5), quantile(fps, 0.01));

    // What the documented per-account allowances can and cannot do here.
    //
    // While the view timers do not add up, an account's *documented* allowance cannot fail a
    // run: `present` would fail every one of them, because the wait for the drawable lands
    // in whichever view presents. So the documented figures stay advisory and the frame is
    // what is enforced.
    //
    // An allowance given on the command line is different. `--budget shade=1.0` is a claim
    // the caller is making about this run, and a claim that cannot fail is not a gate — that
    // was sprint 0's own proving sentence, and it had quietly stopped being true. An
    // override is checked against the share, coherent timers or not, and `gpu` and `cpu`
    // can be overridden by name so the gate can be failed on a figure measured directly.
    bool claimBroken = false;
    for (const auto& o : overrides_) {
        double measured = 0.0;
        bool found = false;
        if (o.account == "gpu") {
            measured = gpuMed;
            found = true;
        } else if (o.account == "cpu") {
            measured = cpuMed;
            found = true;
        } else {
            for (int a = 0; a < AccountCount; ++a) {
                if (o.account != accountName(Account(a))) continue;
                measured = medianSum > 0.0 ? quantile(perAccount[a], 0.5) / medianSum * gpuMed
                                           : 0.0;
                found = true;
            }
        }
        if (!found) {
            core::logError("--budget names '%s', which is not an account", o.account.c_str());
            claimBroken = true;
            continue;
        }
        if (measured > o.ms) {
            core::logf("%s was claimed at %.3f ms and measured %.3f", o.account.c_str(), o.ms,
                       measured);
            claimBroken = true;
        }
    }

    const bool overdrawn = gpuOver || cpuOver || claimBroken;
    if (!enforce) return true;
    if (overdrawn) core::logError("the budget is overdrawn; see docs/budget.md");
    return !overdrawn;
}

}  // namespace mu::gfx
