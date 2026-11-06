#include <tinyme/core/TimingStatistics.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace tinyme
{
TimingStatistics SummarizeTimings(std::span<const double> milliseconds)
{
    TimingStatistics result;
    if (milliseconds.empty())
    {
        return result;
    }
    std::vector<double> ordered(milliseconds.begin(), milliseconds.end());
    for (const auto value : ordered)
    {
        if (!std::isfinite(value) || value < 0.0)
        {
            throw std::invalid_argument("Timing samples must be finite nonnegative milliseconds.");
        }
        result.mean_ms += value / static_cast<double>(ordered.size());
    }
    std::sort(ordered.begin(), ordered.end());
    result.count = ordered.size();
    result.min_ms = ordered.front();
    result.max_ms = ordered.back();
    const auto middle = ordered.size() / 2;
    result.median_ms =
        ordered.size() % 2 ? ordered[middle] : ordered[middle - 1] / 2.0 + ordered[middle] / 2.0;
    const auto rank =
        static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(ordered.size())));
    result.p95_ms = ordered[rank - 1];
    return result;
}
} // namespace tinyme
