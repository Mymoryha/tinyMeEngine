#include <tinyme/core/LaunchOptions.h>
#include <tinyme/runtime/Session.h>

#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace
{
int failures = 0;
int checks = 0;

void Check(bool condition, std::string_view description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

tinyme::ParseResult Parse(std::initializer_list<std::string_view> arguments)
{
    return tinyme::ParseCommandLine(
        std::span<const std::string_view>(arguments.begin(), arguments.size()));
}

void TestOptions()
{
    using tinyme::LaunchAction;
    Check(Parse({}).options.action == LaunchAction::Help, "no arguments show help");
    Check(Parse({"--help"}).options.action == LaunchAction::Help, "long help");
    Check(Parse({"-h"}).options.action == LaunchAction::Help, "short help");
    Check(Parse({"--version"}).options.action == LaunchAction::Version, "version");

    const auto defaults = Parse({"--headless"});
    Check(defaults.IsValid() && defaults.options.action == LaunchAction::Run &&
              defaults.options.frame_count == 1,
          "headless defaults to one tick");
    const auto forward = Parse({"--headless", "--frames", "3"});
    Check(forward.IsValid() && forward.options.frame_count == 3, "frame count");
    const auto reverse = Parse({"--frames", "3", "--headless"});
    Check(reverse.IsValid() && reverse.options.frame_count == 3, "argument order independent");
    Check(Parse({"--headless", "--frames", "1"}).IsValid(), "minimum budget");
    const auto maximum = Parse({"--headless", "--frames", "1000000"});
    Check(maximum.IsValid() && maximum.options.frame_count == tinyme::kMaxHeadlessFrames,
          "maximum budget");

    Check(!Parse({"--frames", "2"}).IsValid(), "execution requires explicit headless");
    Check(!Parse({"--headless", "--frames"}).IsValid(), "missing value rejected");
    Check(!Parse({"--headless", "--frames", ""}).IsValid(), "empty value rejected");
    Check(!Parse({"--headless", "--frames", "0"}).IsValid(), "zero budget rejected");
    Check(!Parse({"--headless", "--frames", "-1"}).IsValid(), "negative budget rejected");
    Check(!Parse({"--headless", "--frames", "+1"}).IsValid(), "signed text rejected");
    Check(!Parse({"--headless", "--frames", " 1"}).IsValid(), "whitespace rejected");
    Check(!Parse({"--headless", "--frames", "1x"}).IsValid(), "trailing text rejected");
    Check(!Parse({"--headless", "--frames", "1.5"}).IsValid(), "fraction rejected");
    Check(!Parse({"--headless", "--frames", "1000001"}).IsValid(), "over budget rejected");
    Check(!Parse({"--headless", "--frames", "4294967296"}).IsValid(), "integer overflow rejected");
    Check(!Parse({"--headless", "--headless"}).IsValid(), "duplicate mode rejected");
    Check(!Parse({"--headless", "--frames", "1", "--frames", "2"}).IsValid(),
          "duplicate count rejected");
    Check(!Parse({"--help", "--headless"}).IsValid(), "conflicting help rejected");
    Check(!Parse({"--version", "--headless"}).IsValid(), "conflicting version rejected");
    Check(!Parse({"--unknown"}).IsValid(), "unknown flag rejected");
    Check(!Parse({"--headless", "extra"}).IsValid(), "positional argument rejected");
    using tinyme::ExecutionMode;
    const auto windowed = Parse({"--windowed"});
    Check(windowed.IsValid() && windowed.options.mode == ExecutionMode::WindowedGpu &&
              !windowed.options.has_frame_limit,
          "interactive window mode");
    const auto limited = Parse({"--frames", "3", "--windowed"});
    Check(limited.IsValid() && limited.options.has_frame_limit && limited.options.frame_count == 3,
          "finite window execution");
    Check(defaults.options.mode == ExecutionMode::HeadlessLogic, "headless stays GPU-free");
    Check(Parse({"--windowed", "--debug-layer"}).IsValid(), "explicit debug layer");
    Check(Parse({"--gpu-validation", "--windowed"}).IsValid(), "GPU validation option order");
    Check(Parse({"--windowed", "--gpu-validation", "--debug-layer"}).IsValid(),
          "validation flags coexist");
    Check(!Parse({"--windowed", "--headless"}).IsValid(), "conflicting modes rejected");
    Check(!Parse({"--windowed", "--windowed"}).IsValid(), "duplicate window mode rejected");
    Check(!Parse({"--headless", "--debug-layer"}).IsValid(), "headless rejects debug layer");
    Check(!Parse({"--headless", "--gpu-validation"}).IsValid(), "headless rejects GPU validation");
    Check(!Parse({"--windowed", "--debug-layer", "--debug-layer"}).IsValid(),
          "duplicate debug option rejected");
    Check(!Parse({"--windowed", "--gpu-validation", "--gpu-validation"}).IsValid(),
          "duplicate GBV option rejected");
    Check(!Parse({"--windowed", "--frames", "0"}).IsValid(),
          "explicit zero is not interactive unlimited");
    Check(!Parse({"--windowed", "--help"}).IsValid(), "window conflicts with help");
    Check(Parse({"--windowed"}).options.experiment == "triangle", "default triangle");
    Check(Parse({"--windowed", "--experiment", "clear"}).IsValid(), "clear regression mode");
    Check(Parse({"--shader", "a path.hlsl", "--windowed"}).IsValid(), "explicit shader path");
    Check(!Parse({"--headless", "--shader", "a.hlsl"}).IsValid(),
          "shader never initializes headless GPU");
    Check(!Parse({"--headless", "--experiment", "triangle"}).IsValid(),
          "headless rejects experiments");
    Check(!Parse({"--windowed", "--experiment", "unknown"}).IsValid(),
          "unknown experiment rejected");
    Check(!Parse({"--windowed", "--experiment", "clear", "--shader", "a.hlsl"}).IsValid(),
          "clear rejects shader");
    Check(!Parse({"--windowed", "--shader"}).IsValid(), "missing shader path");
    Check(!Parse({"--windowed", "--shader", ""}).IsValid(), "empty shader path");
    Check(!Parse({"--windowed", "--shader", "--frames", "8"}).IsValid(),
          "flag not interpreted as path");
    Check(!Parse({"--windowed", "--shader", "a", "--shader", "b"}).IsValid(),
          "duplicate shader path");
    Check(!Parse({"--windowed", "--experiment"}).IsValid(), "missing experiment value");
    Check(!Parse({"--windowed", "--experiment", "clear", "--experiment", "triangle"}).IsValid(),
          "duplicate experiment");
}

void TestSession()
{
    tinyme::Session session;
    Check(session.GetStats().tick_count == 0 && session.GetStats().simulated_seconds == 0.0,
          "session starts empty");
    Check(session.Tick(1.0 / 60.0), "first tick");
    Check(session.Tick(1.0 / 60.0), "second tick");
    Check(session.Tick(1.0 / 60.0), "third tick");
    Check(session.GetStats().tick_count == 3 &&
              std::abs(session.GetStats().simulated_seconds - 0.05) < 1e-12,
          "deterministic simulated time");

    const auto before = session.GetStats();
    Check(!session.Tick(-1.0), "negative delta rejected");
    Check(!session.Tick(std::numeric_limits<double>::quiet_NaN()), "NaN rejected");
    Check(!session.Tick(std::numeric_limits<double>::infinity()), "infinity rejected");
    Check(session.GetStats().tick_count == before.tick_count &&
              session.GetStats().simulated_seconds == before.simulated_seconds,
          "failed ticks do not mutate state");
    Check(session.Tick(0.0), "zero timestep accepted");
    Check(session.GetStats().tick_count == before.tick_count + 1 &&
              session.GetStats().simulated_seconds == before.simulated_seconds,
          "zero timestep advances count only");

    tinyme::Session other;
    Check(other.GetStats().tick_count == 0, "sessions are independent");
    Check(other.Tick(std::numeric_limits<double>::max()), "finite large delta accepted");
    Check(!other.Tick(std::numeric_limits<double>::max()), "accumulated time overflow rejected");
    Check(other.GetStats().tick_count == 1 &&
              other.GetStats().simulated_seconds == std::numeric_limits<double>::max(),
          "overflow preserves state");
}
} // namespace

int main()
{
    TestOptions();
    TestSession();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
