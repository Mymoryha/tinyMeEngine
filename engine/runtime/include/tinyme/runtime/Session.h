#pragma once

#include <cstdint>

namespace tinyme
{
struct SessionStats
{
    std::uint64_t tick_count = 0;
    double simulated_seconds = 0.0;
};

// Single-threaded logical session; it owns no window, GPU or editor state.
class Session
{
  public:
    // Invalid or overflowing deltas leave the session unchanged.
    [[nodiscard]] bool Tick(double delta_seconds) noexcept;
    [[nodiscard]] const SessionStats& GetStats() const noexcept;

  private:
    SessionStats stats_;
};
} // namespace tinyme
