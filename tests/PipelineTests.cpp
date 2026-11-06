#include <tinyme/core/LaunchOptions.h>
#include <tinyme/core/RenderPipeline.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace
{
int checks = 0;
int failures = 0;
constexpr std::array<std::string_view, 3> kModes{"--headless", "--windowed", "--offscreen"};
constexpr std::array<std::string_view, 2> kGpuModes{"--windowed", "--offscreen"};

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

void Reject(std::initializer_list<std::string_view> arguments, std::string_view description)
{
    const auto result = Parse(arguments);
    Check(!result.IsValid() && !result.error.empty() &&
              result.options.action != tinyme::LaunchAction::Run,
          description);
    if (result.IsValid())
    {
        std::cerr << "Unexpectedly accepted:";
        for (auto argument : arguments)
        {
            std::cerr << " [" << argument << ']';
        }
        std::cerr << '\n';
    }
}

void TestModesAndFrames()
{
    using tinyme::ExecutionMode;
    using tinyme::LaunchAction;
    Check(Parse({}).IsValid() && Parse({}).options.action == LaunchAction::Help, "empty help");
    for (auto help : {"--help", "-h"})
    {
        Check(Parse({help}).IsValid() && Parse({help}).options.action == LaunchAction::Help,
              "exclusive help");
    }
    Check(Parse({"--version"}).IsValid() &&
              Parse({"--version"}).options.action == LaunchAction::Version,
          "exclusive version");
    constexpr std::array<ExecutionMode, 3> expected{
        ExecutionMode::HeadlessLogic, ExecutionMode::WindowedGpu, ExecutionMode::OffscreenGpu};
    for (std::size_t index = 0; index < kModes.size(); ++index)
    {
        const auto mode = kModes[index];
        const auto defaults = Parse({mode});
        Check(defaults.IsValid() && defaults.options.action == LaunchAction::Run &&
                  defaults.options.mode == expected[index] && defaults.options.frame_count == 1,
              "three explicit execution modes");
        Check(defaults.options.has_frame_limit == (mode == "--offscreen"),
              "only offscreen has an implicit finite GPU budget");
        Check(defaults.options.postprocess == tinyme::PostProcess::None &&
                  defaults.options.post_shader_path.empty() &&
                  defaults.options.capture_path.empty() && defaults.options.width == 1280 &&
                  defaults.options.height == 720 && defaults.options.experiment == "triangle" &&
                  defaults.options.shader_path.empty(),
              "launch defaults preserve the direct triangle path");
        for (auto other : kModes)
        {
            Reject({mode, other}, "all execution-mode pairs including duplicates conflict");
        }
        for (auto exclusive : {"--help", "-h", "--version"})
        {
            Reject({mode, exclusive}, "help/version cannot follow a mode");
            Reject({exclusive, mode}, "help/version cannot precede a mode");
        }
        for (auto value : {"1", "1000000"})
        {
            const auto finite = Parse({mode, "--frames", value});
            const auto reverse = Parse({"--frames", value, mode});
            Check(finite.IsValid() && finite.options.has_frame_limit && reverse.IsValid() &&
                      reverse.options.has_frame_limit &&
                      reverse.options.frame_count == finite.options.frame_count &&
                      finite.options.frame_count ==
                          (value == std::string_view("1") ? 1U : 1000000U),
                  "explicit frame boundaries and argument order");
        }
        for (auto value : {"", "0", "-1", "+1", " 1", "1 ", "1x", "1.5", "1e2", "0x10", "1000001",
                           "4294967296", "999999999999999999999"})
        {
            Reject({mode, "--frames", value}, "strict frame count");
        }
        Reject({mode, "--frames"}, "missing frame count");
        Reject({mode, "--frames", "--profile"}, "frame count cannot consume a flag");
        Reject({mode, "--frames", "1", "--frames", "2"}, "duplicate frame count");
        Reject({mode, "--unknown"}, "unknown flags remain errors");
        Reject({mode, "extra"}, "positional arguments remain errors");
    }
    Reject({"--frames", "1"}, "frames require an explicit mode");
    Reject({"--help", "--version"}, "help and version conflict");
    Reject({"--help", "--help"}, "duplicate help is not exclusive help");
    Reject({"--version", "--version"}, "duplicate version is invalid");
}

void TestPipelineAndDimensions()
{
    using tinyme::PostProcess;
    constexpr std::array<PostProcess, 3> values{PostProcess::None, PostProcess::Copy,
                                                PostProcess::Grayscale};
    constexpr std::array<std::string_view, 3> names{"none", "copy", "grayscale"};
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        Check(tinyme::IsValid(values[index]) && tinyme::ToString(values[index]) == names[index],
              "postprocess enum contract");
        for (auto mode : kGpuModes)
        {
            const auto selected = Parse({mode, "--postprocess", names[index]});
            Check(selected.IsValid() && selected.options.postprocess == values[index],
                  "GPU modes select each postprocess");
            Check(Parse({"--postprocess", names[index], "--experiment", "clear", mode}).IsValid(),
                  "clear accepts each postprocess independent of option order");
            Reject({mode, "--postprocess", names[index], "--postprocess", names[index]},
                   "duplicate postprocess including explicit none");
        }
        Reject({"--headless", "--postprocess", names[index]}, "headless rejects every postprocess");
    }
    for (auto value : {3U, std::numeric_limits<std::uint32_t>::max()})
    {
        const auto invalid = static_cast<PostProcess>(value);
        Check(!tinyme::IsValid(invalid) && tinyme::ToString(invalid) == "invalid",
              "invalid postprocess enum values are observable");
    }
    Check(tinyme::kMaxRenderDimension == 2048, "fixed render dimension limit");
    for (auto mode : kGpuModes)
    {
        Reject({mode, "--postprocess"}, "missing postprocess value");
        for (auto value : {"", "None", "COPY", "gray", "unknown", " copy", "copy ", "--frames"})
        {
            Reject({mode, "--postprocess", value}, "postprocess names are strict");
        }
        Reject({mode, "--postprocess", "none", "--postprocess", "copy"},
               "different postprocess names still duplicate");
        for (auto option : {"--width", "--height"})
        {
            for (auto text : {"1", "2048"})
            {
                const auto dimension = Parse({option, text, mode});
                const auto expected = text == std::string_view("1") ? 1U : 2048U;
                Check(dimension.IsValid() &&
                          dimension.options.width ==
                              (option == std::string_view("--width") ? expected : 1280U) &&
                          dimension.options.height ==
                              (option == std::string_view("--height") ? expected : 720U),
                      "single dimensions accept bounds and preserve the other default");
            }
            Reject({mode, option}, "missing dimension");
            Reject({mode, option, "1", option, "2"}, "duplicate dimension");
            for (auto value : {"", "0", "-1", "+1", " 1", "1 ", "1x", "1.5", "1e2", "0x10", "2049",
                               "4294967296", "999999999999999999999", "--frames"})
            {
                Reject({mode, option, value}, "strict bounded render dimensions");
            }
            Reject({"--headless", option, "1"}, "headless rejects explicit dimensions");
            Reject({option, "1"}, "dimensions require an explicit mode");
        }
        const auto rectangular = Parse({mode, "--width", "2048", "--height", "1"});
        Check(rectangular.IsValid() && rectangular.options.width == 2048 &&
                  rectangular.options.height == 1,
              "dimensions are independently bounded, not forced to an aspect ratio");
        Check(Parse({mode, "--experiment", "clear", "--width", "1", "--height", "2048"}).IsValid(),
              "clear accepts dimensions");
        for (auto postprocess : {"copy", "grayscale"})
        {
            const auto custom = Parse(
                {"--post-shader", "shaders/post path.hlsl", mode, "--postprocess", postprocess});
            Check(custom.IsValid() && custom.options.post_shader_path == "shaders/post path.hlsl",
                  "custom post shader is independent of argument order");
            Check(Parse({mode, "--experiment", "clear", "--postprocess", postprocess,
                         "--post-shader", "shaders/后处理.hlsl"})
                      .IsValid(),
                  "clear accepts a custom post shader with postprocessing");
            Reject({mode, "--postprocess", postprocess, "--post-shader", "a.hlsl", "--post-shader",
                    "b.hlsl"},
                   "duplicate post shader");
            Reject({mode, "--postprocess", postprocess, "--post-shader"}, "missing post shader");
            for (auto path : {"", "--frames", "--postprocess", "-h", "--version"})
            {
                Reject({mode, "--postprocess", postprocess, "--post-shader", path},
                       "post shader path must not be empty or consume a flag");
            }
        }
        Reject({mode, "--post-shader", "a.hlsl"}, "default none rejects post shader");
        Reject({mode, "--postprocess", "none", "--post-shader", "a.hlsl"},
               "explicit none rejects post shader");
    }
    Reject({"--postprocess", "copy"}, "postprocess requires a mode");
    Reject({"--headless", "--postprocess", "copy", "--post-shader", "a.hlsl"},
           "headless rejects custom postprocessing");
    Reject({"--postprocess", "copy", "--post-shader", "a.hlsl"}, "post shader requires a mode");
}

void TestCaptureAndBudgets()
{
    for (auto path : {"a.png", "a.pnG", "a.pNg", "a.pNG", "a.Png", "a.PnG", "a.PNg", "a.PNG",
                      "out/capture path.PNG", "out\\capture.png", "out/图像.png"})
    {
        const auto capture = Parse({"--capture", path, "--offscreen"});
        Check(capture.IsValid() && capture.options.capture_path == path &&
                  capture.options.has_frame_limit && capture.options.frame_count == 1,
              "PNG capture uses the finite one-frame offscreen default");
        Reject({"--windowed", "--capture", path}, "windowed rejects capture");
        Reject({"--headless", "--capture", path}, "headless rejects capture");
    }
    Reject({"--capture", "a.png"}, "capture requires a mode");
    Reject({"--offscreen", "--capture"}, "missing capture path");
    Reject({"--offscreen", "--capture", "a.png", "--capture", "b.png"}, "duplicate capture path");
    for (auto path : {"", "a", "a.jpg", "a.png.jpg", "a.png/", "a.png ", ".png", "out/.png",
                      "out\\.png", "--frames", "--capture", "-h", "--version"})
    {
        Reject({"--offscreen", "--capture", path},
               "capture requires a nonempty PNG path, not a flag");
    }
    Reject({"--offscreen", "--profile", "--capture", "same.png", "--report", "same.png"},
           "capture and report cannot have identical paths");
    Reject({"--report", "same.PNG", "--profile", "--capture", "same.PNG", "--offscreen"},
           "same-path rejection is order independent");
    const auto both = Parse({"--offscreen", "--profile", "--warmup", "0", "--samples", "1",
                             "--capture", "out/image.png", "--report", "out/report.json"});
    Check(both.IsValid() && both.options.frame_count == 1 && both.options.has_frame_limit &&
              both.options.capture_path == "out/image.png" &&
              both.options.report_path == "out/report.json",
          "capture and distinct report coexist at the minimum profile budget");
    Check(Parse({"--offscreen", "--capture", "a.png", "--frames", "3"}).IsValid(),
          "capture accepts an explicit multi-frame offscreen run");
    Check(Parse({"--offscreen", "--experiment", "clear", "--postprocess", "grayscale", "--capture",
                 "a.png"})
              .IsValid(),
          "clear and postprocessing can be captured");
    for (auto mode : kGpuModes)
    {
        const auto defaults = Parse({mode, "--profile"});
        Check(defaults.IsValid() && defaults.options.profile.enabled &&
                  defaults.options.profile.warmup_frames == 30 &&
                  defaults.options.profile.sample_frames == 120 &&
                  defaults.options.frame_count == 150 && defaults.options.has_frame_limit,
              "profile budget supersedes the offscreen one-frame default");
        const auto configured = Parse({"--samples", "3", "--warmup", "2", "--report",
                                       "out/report path.json", mode, "--profile"});
        Check(configured.IsValid() && configured.options.frame_count == 5 &&
                  configured.options.report_path == "out/report path.json",
              "configured profile budgets are order independent");
        const auto maximum =
            Parse({mode, "--profile", "--warmup", "100000", "--samples", "100000"});
        Check(maximum.IsValid() && maximum.options.frame_count == 200000 &&
                  maximum.options.has_frame_limit,
              "maximum warmup plus sample budget");
        Check(Parse({mode, "--profile", "--warmup", "0", "--samples", "1"}).IsValid(),
              "zero warmup and one sample");
        Check(Parse({mode, "--profile", "--frames", "150"}).IsValid(), "exact frame budget");
        Check(Parse({mode, "--experiment", "clear", "--profile", "--frames", "151"}).IsValid(),
              "clear profile allows extra frames");
        Reject({mode, "--profile", "--frames", "149"}, "explicit frames must cover default budget");
        Reject({mode, "--profile", "--warmup", "1", "--samples", "1", "--frames", "1"},
               "one explicit offscreen frame cannot undercut the profile budget");
        Reject({mode, "--profile", "--profile"}, "duplicate profile");
        for (auto option : {"--warmup", "--samples"})
        {
            Reject({mode, option, "1"}, "sample budget requires profile");
            Reject({mode, "--profile", option}, "missing sample budget");
            Reject({mode, "--profile", option, "1", option, "1"}, "duplicate sample budget");
            for (auto value : {"", "-1", "+1", " 1", "1 ", "1x", "1.5", "1e2", "100001",
                               "4294967296", "--frames"})
            {
                Reject({mode, "--profile", option, value}, "strict sample budgets");
            }
        }
        Reject({mode, "--profile", "--samples", "0"}, "zero samples rejected");
        Reject({mode, "--report", "a.json"}, "report requires profile");
        Reject({mode, "--profile", "--report"}, "missing report path");
        Reject({mode, "--profile", "--report", ""}, "empty report path");
        Reject({mode, "--profile", "--report", "--frames", "1"},
               "report cannot consume a long flag");
        Reject({mode, "--profile", "--report", "a", "--report", "b"}, "duplicate report path");
    }
}

void TestLegacyGpuOptions()
{
    for (auto mode : kGpuModes)
    {
        const auto configured = Parse({mode,
                                       "--experiment",
                                       "triangle",
                                       "--shader",
                                       "shaders/三角形 path.hlsl",
                                       "--scale",
                                       "5e-1",
                                       "--tint",
                                       "0.1",
                                       "0.2",
                                       "0.3",
                                       "0.4",
                                       "--camera-eye",
                                       "0.25",
                                       "0.125",
                                       "-3",
                                       "--camera-target",
                                       "0.25",
                                       "0.125",
                                       "0",
                                       "--fov",
                                       "75",
                                       "--debug-layer",
                                       "--gpu-validation"});
        Check(configured.IsValid() && configured.options.parameters.scale == 0.5F &&
                  configured.options.parameters.tint ==
                      std::array<float, 4>{0.1F, 0.2F, 0.3F, 0.4F} &&
                  configured.options.camera.eye == std::array<float, 3>{0.25F, 0.125F, -3.0F} &&
                  configured.options.camera.target == std::array<float, 3>{0.25F, 0.125F, 0.0F} &&
                  configured.options.camera.vertical_fov_degrees == 75 &&
                  configured.options.debug_layer && configured.options.gpu_validation,
              "both GPU modes preserve all legacy triangle inputs");
        for (auto option : {"--debug-layer", "--gpu-validation"})
        {
            Check(Parse({option, mode}).IsValid(), "validation flag can precede either GPU mode");
            Reject({mode, option, option}, "duplicate validation flags");
        }
        Check(Parse({mode, "--experiment", "clear"}).IsValid(), "legacy clear path");
        Reject({mode, "--experiment", "unknown"}, "unknown experiment");
        Reject({mode, "--experiment"}, "missing experiment");
        Reject({mode, "--experiment", ""}, "empty experiment");
        Reject({mode, "--experiment", "--frames", "1"}, "experiment cannot consume a long flag");
        Reject({mode, "--experiment", "clear", "--experiment", "triangle"}, "duplicate experiment");
        Reject({mode, "--experiment", "clear", "--shader", "a.hlsl"},
               "clear rejects triangle shader");
        Reject({mode, "--shader"}, "missing triangle shader");
        Reject({mode, "--shader", ""}, "empty triangle shader");
        Reject({mode, "--shader", "--frames", "1"}, "triangle shader cannot consume a long flag");
        Reject({mode, "--shader", "a", "--shader", "b"}, "duplicate triangle shader");
        for (auto value : {"0.01", "2"})
        {
            Check(Parse({mode, "--scale", value}).IsValid(), "legacy scale bounds");
        }
        for (auto value :
             {"", "nan", "inf", "-inf", "0", "-1", "2.01", "1e99", "1x", " 1", "+1", "--frames"})
        {
            Reject({mode, "--scale", value}, "strict finite legacy scale");
        }
        Reject({mode, "--scale"}, "missing scale");
        Reject({mode, "--scale", "1", "--scale", "1"}, "duplicate scale");
        Check(Parse({mode, "--tint", "0", "1", "0", "1"}).IsValid(), "legacy tint bounds");
        for (auto value : {"", "nan", "inf", "-0.01", "1.01", "1x", " 1", "+1"})
        {
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                std::array<std::string_view, 4> rgba{"1", "1", "1", "1"};
                rgba[channel] = value;
                Reject({mode, "--tint", rgba[0], rgba[1], rgba[2], rgba[3]},
                       "each tint channel is strict");
            }
        }
        Reject({mode, "--tint", "1", "1", "1"}, "missing tint alpha");
        Reject({mode, "--tint", "1", "1", "1", "1", "--tint", "1", "1", "1", "1"},
               "duplicate tint");
        Reject({mode, "--experiment", "clear", "--scale", "1"}, "clear rejects scale");
        Reject({mode, "--experiment", "clear", "--tint", "1", "1", "1", "1"}, "clear rejects tint");
        for (auto option : {"--camera-eye", "--camera-target"})
        {
            Check(Parse({mode, option, "10000", "-10000", "-3"}).IsValid(),
                  "camera coordinate bounds");
            Reject({mode, option, "0", "0"}, "incomplete camera vector");
            Reject({mode, option, "0", "0", "-3", option, "0", "0", "-3"},
                   "duplicate camera vector");
            Reject({mode, "--experiment", "clear", option, "0", "0", "-3"},
                   "clear rejects camera vectors");
            for (auto value : {"", "nan", "inf", "-inf", "10000.1", "-10000.1", "1e99", "1x", " 1",
                               "+1", "--frames"})
            {
                for (std::size_t component = 0; component < 3; ++component)
                {
                    std::array<std::string_view, 3> xyz{"0", "0", "-3"};
                    xyz[component] = value;
                    Reject({mode, option, xyz[0], xyz[1], xyz[2]},
                           "each camera coordinate is strict");
                }
            }
        }
        for (auto value : {"10", "120"})
        {
            Check(Parse({mode, "--fov", value}).IsValid(), "legacy FOV bounds");
        }
        for (auto value : {"", "9.99", "120.01", "nan", "inf", "1x", " 10", "+10", "--frames"})
        {
            Reject({mode, "--fov", value}, "strict bounded finite FOV");
        }
        Reject({mode, "--fov"}, "missing FOV");
        Reject({mode, "--fov", "90", "--fov", "90"}, "duplicate FOV");
        Reject({mode, "--experiment", "clear", "--fov", "90"}, "clear rejects FOV");
        Reject({mode, "--camera-eye", "0", "0", "0"},
               "camera eye cannot coincide with default target");
        Reject({mode, "--camera-target", "0", "0", "-2"},
               "camera target cannot coincide with default eye");
        Reject({mode, "--camera-eye", "0", "-2", "0"}, "camera view cannot be parallel to up");
    }
    Reject({"--headless", "--debug-layer"}, "headless rejects debug layer");
    Reject({"--headless", "--gpu-validation"}, "headless rejects GPU validation");
    Reject({"--headless", "--experiment", "triangle"}, "headless rejects explicit triangle");
    Reject({"--headless", "--experiment", "clear"}, "headless rejects explicit clear");
    Reject({"--headless", "--shader", "a.hlsl"}, "headless rejects triangle shader");
    Reject({"--headless", "--scale", "1"}, "headless rejects scale");
    Reject({"--headless", "--tint", "1", "1", "1", "1"}, "headless rejects tint");
    Reject({"--headless", "--camera-eye", "0", "0", "-3"}, "headless rejects camera eye");
    Reject({"--headless", "--camera-target", "0", "0", "0"}, "headless rejects camera target");
    Reject({"--headless", "--fov", "90"}, "headless rejects FOV");
    Reject({"--headless", "--profile"}, "headless rejects profile");
    Reject({"--headless", "--warmup", "0"}, "headless rejects warmup");
    Reject({"--headless", "--samples", "1"}, "headless rejects samples");
    Reject({"--headless", "--report", "a.json"}, "headless rejects report");
}
} // namespace

int main()
{
    TestModesAndFrames();
    TestPipelineAndDimensions();
    TestCaptureAndBudgets();
    TestLegacyGpuOptions();
    std::cout << "pipeline_contracts checks=" << checks << " failures=" << failures
              << " no_gpu=1\n";
    return failures == 0 ? 0 : 1;
}
