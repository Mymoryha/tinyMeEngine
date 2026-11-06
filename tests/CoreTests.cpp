#include <tinyme/core/LaunchOptions.h>
#include <tinyme/core/TimingStatistics.h>
#include <tinyme/runtime/Session.h>

#include <array>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
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

void TestParametersAndProfile()
{
    tinyme::TriangleParameters parameters;
    Check(tinyme::IsValid(parameters) && parameters.scale == 1.0F &&
              parameters.tint == std::array<float, 4>{1, 1, 1, 1},
          "default triangle parameters preserve the original image");
    for (float value : {0.01F, 2.0F})
    {
        parameters.scale = value;
        Check(tinyme::IsValid(parameters), "scale boundary accepted");
    }
    for (float value : {0.0F, -1.0F, 2.01F, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()})
    {
        parameters.scale = value;
        Check(!tinyme::IsValid(parameters), "invalid scale rejected");
    }
    parameters = {};
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        for (float value : {0.0F, 1.0F})
        {
            parameters.tint[channel] = value;
            Check(tinyme::IsValid(parameters), "tint boundary accepted");
        }
        for (float value : {-0.01F, 1.01F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()})
        {
            parameters.tint[channel] = value;
            Check(!tinyme::IsValid(parameters), "invalid tint channel rejected");
        }
        parameters = {};
    }
    parameters.reserved[2] = 1.0F;
    Check(!tinyme::IsValid(parameters), "nonzero reserved parameter rejected");
    parameters.reserved[2] = std::numeric_limits<float>::quiet_NaN();
    Check(!tinyme::IsValid(parameters), "NaN reserved parameter rejected");

    const auto configured =
        Parse({"--tint", "0.1", "0.2", "0.3", "0.4", "--scale", "5e-1", "--windowed"});
    Check(configured.IsValid() && configured.options.parameters.scale == 0.5F &&
              configured.options.parameters.tint == std::array<float, 4>{0.1F, 0.2F, 0.3F, 0.4F},
          "RGBA and scientific scale parsed independently of option order");
    for (auto value : {"", "nan", "inf", "-inf", "0", "-1", "2.01", "1e99", "1x", " 1", "+1"})
    {
        Check(!Parse({"--windowed", "--scale", value}).IsValid(), "strict scale parsing");
    }
    for (auto value : {"nan", "inf", "-0.01", "1.01", "1x"})
    {
        Check(!Parse({"--windowed", "--tint", "1", "1", "1", value}).IsValid(),
              "strict tint parsing");
    }
    Check(!Parse({"--windowed", "--scale"}).IsValid(), "missing scale");
    Check(!Parse({"--windowed", "--tint", "1", "1", "1"}).IsValid(), "missing tint alpha");
    Check(!Parse({"--windowed", "--scale", "1", "--scale", "1"}).IsValid(), "duplicate scale");
    Check(!Parse({"--windowed", "--tint", "1", "1", "1", "1", "--tint", "1", "1", "1", "1"})
               .IsValid(),
          "duplicate tint");
    Check(!Parse({"--headless", "--scale", "1"}).IsValid(), "headless rejects parameters");
    Check(!Parse({"--windowed", "--experiment", "clear", "--tint", "1", "1", "1", "1"}).IsValid(),
          "clear rejects triangle parameters");
    const auto profile = Parse({"--windowed", "--profile"});
    Check(profile.IsValid() && profile.options.has_frame_limit &&
              profile.options.frame_count == 150 && profile.options.profile.warmup_frames == 30 &&
              profile.options.profile.sample_frames == 120,
          "profile defaults to a finite warmup plus sampling run");
    const auto budget = Parse({"--samples", "3", "--report", "report path.json", "--windowed",
                               "--warmup", "0", "--profile"});
    Check(budget.IsValid() && budget.options.frame_count == 3 &&
              budget.options.report_path == "report path.json",
          "zero warmup and report path supported");
    Check(Parse({"--windowed", "--profile", "--warmup", "100000", "--samples", "100000"}).IsValid(),
          "maximum profile budget");
    Check(Parse({"--windowed", "--experiment", "clear", "--profile", "--frames", "151"}).IsValid(),
          "clear profiling and longer explicit run");
    Check(!Parse({"--windowed", "--profile", "--frames", "149"}).IsValid(), "insufficient frames");
    Check(!Parse({"--headless", "--profile"}).IsValid(), "headless rejects profile");
    Check(!Parse({"--windowed", "--profile", "--profile"}).IsValid(), "duplicate profile");
    for (auto option : {"--warmup", "--samples"})
    {
        Check(!Parse({"--windowed", option, "1"}).IsValid(), "budget requires profile");
        Check(!Parse({"--windowed", "--profile", option}).IsValid(), "missing budget");
        Check(!Parse({"--windowed", "--profile", option, "1", option, "1"}).IsValid(),
              "duplicate budget");
        for (auto value : {"", "-1", "+1", "1x", "100001", "4294967296"})
        {
            Check(!Parse({"--windowed", "--profile", option, value}).IsValid(), "invalid budget");
        }
    }
    Check(!Parse({"--windowed", "--profile", "--samples", "0"}).IsValid(),
          "samples cannot be zero");
    Check(!Parse({"--windowed", "--report", "a.json"}).IsValid(), "report requires profile");
    Check(!Parse({"--windowed", "--profile", "--report"}).IsValid(), "missing report path");
    Check(!Parse({"--windowed", "--profile", "--report", ""}).IsValid(), "empty report path");
    Check(!Parse({"--windowed", "--profile", "--report", "--samples", "1"}).IsValid(),
          "report path cannot consume a flag");
    Check(!Parse({"--windowed", "--profile", "--report", "a", "--report", "b"}).IsValid(),
          "duplicate report path");
    Check(!tinyme::IsValid(tinyme::ProfileOptions{true, 0, 0}) &&
              !tinyme::IsValid(tinyme::ProfileOptions{true, 100001, 1}),
          "programmatic profile budgets checked");
}

void TestTimings()
{
    const auto empty = tinyme::SummarizeTimings({});
    Check(empty.count == 0, "empty timing summary");
    const std::array<double, 4> samples{4, 1, 3, 2};
    const auto summary = tinyme::SummarizeTimings(samples);
    Check(summary.count == 4 && summary.mean_ms == 2.5 && summary.median_ms == 2.5 &&
              summary.p95_ms == 4 && summary.min_ms == 1 && summary.max_ms == 4,
          "unsorted even timings, nearest-rank P95");
    const std::array<double, 3> odd{3, 1, 2};
    Check(tinyme::SummarizeTimings(odd).median_ms == 2, "odd median");
    const std::array<double, 1> zero{0};
    Check(tinyme::SummarizeTimings(zero).count == 1 && tinyme::SummarizeTimings(zero).p95_ms == 0,
          "single zero timing accepted");
    std::array<double, 20> ranks{};
    for (std::size_t i = 0; i < ranks.size(); ++i)
    {
        ranks[i] = static_cast<double>(i + 1);
    }
    Check(tinyme::SummarizeTimings(ranks).p95_ms == 19, "P95 uses nearest rank, not interpolation");
    for (double value :
         {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        bool rejected = false;
        try
        {
            const std::array<double, 1> invalid{value};
            static_cast<void>(tinyme::SummarizeTimings(invalid));
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Check(rejected, "invalid timing sample rejected");
    }
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
    TestParametersAndProfile();
    TestTimings();
    TestSession();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
