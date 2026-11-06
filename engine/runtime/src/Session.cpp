#include <tinyme/runtime/Session.h>

#include <cmath>
#include <limits>

namespace tinyme
{
bool Session::Tick(double delta_seconds) noexcept
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0 ||
        stats_.tick_count == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    const double next_time = stats_.simulated_seconds + delta_seconds;
    if (!std::isfinite(next_time))
    {
        return false;
    }
    ++stats_.tick_count;
    stats_.simulated_seconds = next_time;
    return true;
}

const SessionStats& Session::GetStats() const noexcept
{
    return stats_;
}
} // namespace tinyme
