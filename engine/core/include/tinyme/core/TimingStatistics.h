#pragma once

#include <cstddef>
#include <span>

namespace tinyme
{
struct TimingStatistics
{
    std::size_t count = 0;
    double mean_ms = 0.0;
    double median_ms = 0.0;
    double p95_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
};
// Nonnegative finite milliseconds; P95 uses nearest rank, median averages the middle pair.
[[nodiscard]] TimingStatistics SummarizeTimings(std::span<const double> milliseconds);
} // namespace tinyme
