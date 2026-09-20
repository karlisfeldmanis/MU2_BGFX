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

    core::logf("--- %zu frames measured, %zu warmup dropped ---", n, first);
    core::logf("%-10s %8s %8s %8s", "account", "median", "p99", "budget");
    bool overdrawn = false;
    double medianSum = 0.0;
    for (int a = 0; a < AccountCount; ++a) {
        const double med = quantile(perAccount[a], 0.5);
        const double p99 = quantile(perAccount[a], 0.99);
        const double budget = allowance(Account(a));
        medianSum += med;
        const bool over = med > budget;
        overdrawn = overdrawn || over;
        core::logf("%-10s %8.3f %8.3f %8.3f%s", accountName(Account(a)), med, p99, budget,
                   over ? "  OVERDRAWN" : "");
    }
    core::logf("%-10s %8.3f %8.3f %8.3f", "gpu total", quantile(gpu, 0.5), quantile(gpu, 0.99),
               medianSum + spareBudgetMs());
    const double cpuMed = quantile(cpu, 0.5);
    const bool cpuOver = cpuMed > cpuBudgetMs();
    overdrawn = overdrawn || cpuOver;
    core::logf("%-10s %8.3f %8.3f %8.3f%s", "cpu", cpuMed, quantile(cpu, 0.99), cpuBudgetMs(),
               cpuOver ? "  OVERDRAWN" : "");
    core::logf("%-10s %8.1f %8.1f", "fps", quantile(fps, 0.5), quantile(fps, 0.01));

    if (!enforce) return true;
    if (overdrawn) core::logError("the budget is overdrawn; see docs/budget.md");
    return !overdrawn;
}

}  // namespace mu::gfx
