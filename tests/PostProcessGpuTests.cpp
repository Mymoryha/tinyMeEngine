#include <tinyme/render/d3d12/ClearRenderer.h>

#include "ShaderFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
int checks = 0;
constexpr std::array<float, 4> kClear{0.025F, 0.055F, 0.09F, 1.0F};
void Check(bool value, const char* description)
{
    ++checks;
    if (!value)
    {
        throw std::runtime_error(description);
    }
}
template <class Function> void Reject(Function function, const char* description)
{
    bool rejected = false;
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, description);
}
void Compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, bool grayscale)
{
    Check(a.size() == b.size() && !a.empty() && a.size() % 4 == 0,
          "Packed full-image byte counts agree");
    int maximum = 0;
    for (std::size_t offset = 0; offset < a.size(); offset += 4)
    {
        const int gray = static_cast<int>(
            std::lround(a[offset] * .2126 + a[offset + 1] * .7152 + a[offset + 2] * .0722));
        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            const int expected = grayscale && channel < 3 ? gray : a[offset + channel];
            maximum = std::max(maximum, std::abs(static_cast<int>(b[offset + channel]) - expected));
        }
        if (grayscale)
        {
            Check(b[offset] == b[offset + 1] && b[offset] == b[offset + 2],
                  "Grayscale channels exactly agree");
        }
    }
    Check(maximum <= 1, "Full-image independent copy/grayscale reference max error <=1 UNORM byte");
    std::cout << "image_compare grayscale=" << grayscale << " max_error=" << maximum
              << " bytes=" << a.size() << '\n';
}
void TestPipeline(bool gbv)
{
    tinyme::tests::ShaderFixture fixture;
    const auto directory = tinyme::tests::ExecutableDirectory() / L"shaders";
    const auto scene = directory / L"Triangle.hlsl";
    const auto post = directory / L"PostProcess.hlsl";
    tinyme::ClearRenderer renderer({640, 360}, {true, gbv, true});
    std::string diagnostics;
    Reject([&] { static_cast<void>(renderer.CaptureRgba()); },
           "Cannot capture an unrendered target");
    Reject([&] { renderer.SetPostProcess(tinyme::PostProcess::Copy); },
           "Enabled mode requires valid PSO");
    Check(renderer.ReloadTriangle(scene, diagnostics), "Indexed scene candidate accepted");
    Check(renderer.ReloadPostProcess(post, diagnostics),
          "Full-screen copy/grayscale candidate accepted");
    const auto upload = renderer.GetTriangleGeometryInfo().upload_fence;
    std::uint64_t submitted = 0;
    const auto render =
        [&](const tinyme::TriangleParameters& parameters = tinyme::TriangleParameters{},
            const tinyme::CameraParameters& camera = tinyme::CameraParameters{},
            const std::array<float, 4>& clear = kClear)
    {
        Check(renderer.Render(clear, parameters, camera), "Offscreen GPU frame does not occlude");
        ++submitted;
    };
    render();
    const auto direct = renderer.CaptureRgba();
    const std::size_t center = (180 * 640 + 320) * 4;
    Check(std::abs(static_cast<int>(direct[center]) - 146) <= 2 &&
              std::abs(static_cast<int>(direct[center + 1]) - 93) <= 2 &&
              std::abs(static_cast<int>(direct[center + 2]) - 92) <= 2,
          "Real readback preserves historical RGB reference");
    const std::size_t top = (70 * 640 + 320) * 4;
    const std::size_t bottom = (250 * 640 + 320) * 4;
    Check(direct[top] > direct[bottom] && direct[top] > direct[top + 1] &&
              direct[bottom + 1] > direct[top + 1],
          "RGBA capture orientation is top-down, not vertically flipped");
    renderer.SetPostProcess(tinyme::PostProcess::Copy);
    render();
    Compare(direct, renderer.CaptureRgba(), false);
    renderer.SetPostProcess(tinyme::PostProcess::Grayscale);
    render();
    Compare(direct, renderer.CaptureRgba(), true);
    renderer.SetPostProcess(tinyme::PostProcess::None);
    render();
    Compare(direct, renderer.CaptureRgba(), false);
    for (unsigned index = 0; index < 18; ++index)
    {
        if (index == 0)
            renderer.SetPostProcess(tinyme::PostProcess::Copy);
        if (index == 6)
            renderer.SetPostProcess(tinyme::PostProcess::Grayscale);
        if (index == 12)
            renderer.SetPostProcess(tinyme::PostProcess::None);
        render();
    }
    tinyme::TriangleParameters parameters;
    parameters.scale = .75F;
    parameters.tint = {1, .8F, .6F, .5F};
    tinyme::CameraParameters camera;
    camera.eye = {.25F, .125F, -3};
    camera.target = {.25F, .125F, 0};
    camera.vertical_fov_degrees = 75;
    renderer.Resize({641, 359});
    Reject([&] { static_cast<void>(renderer.CaptureRgba()); },
           "Resize invalidates old capture until Render");
    render(parameters, camera);
    const auto odd = renderer.CaptureRgba();
    Check(odd.size() == 641 * 359 * 4, "Odd width capture strips 256-byte row pitch padding");
    renderer.SetPostProcess(tinyme::PostProcess::Copy);
    render(parameters, camera);
    Compare(odd, renderer.CaptureRgba(), false);
    renderer.SetPostProcess(tinyme::PostProcess::Grayscale);
    render(parameters, camera);
    Compare(odd, renderer.CaptureRgba(), true);
    renderer.Resize({17, 13});
    render();
    Check(renderer.CaptureRgba().size() == 17 * 13 * 4,
          "Tiny GPU target capture uses current dimensions");
    renderer.Resize({640, 360});
    renderer.SetPostProcess(tinyme::PostProcess::Copy);
    const std::array<float, 4> different_clear{.2F, .3F, .4F, .25F};
    render({}, {}, different_clear);
    const auto custom = renderer.CaptureRgba();
    Check(std::abs(static_cast<int>(custom[0]) - 51) <= 1 &&
              std::abs(static_cast<int>(custom[1]) - 77) <= 1 &&
              std::abs(static_cast<int>(custom[2]) - 102) <= 1 &&
              std::abs(static_cast<int>(custom[3]) - 64) <= 1,
          "Changed optimized clear and alpha are correct without hidden GPU warning");
    render();
    Compare(direct, renderer.CaptureRgba(), false);
    const auto generation = renderer.GetPostGeneration();
    fixture.Write(L"syntax.hlsl", "not valid hlsl\n");
    Check(!renderer.ReloadPostProcess(fixture.Path(L"syntax.hlsl"), diagnostics) &&
              !diagnostics.empty(),
          "Post syntax candidate rejected with diagnostics");
    Check(!renderer.ReloadPostProcess(fixture.Path(L"missing.hlsl"), diagnostics),
          "Missing post source rejected");
    Check(renderer.GetPostGeneration() == generation, "Rejected source preserves post generation");
    render();
    Compare(direct, renderer.CaptureRgba(), false);
    fixture.Write(L"unbound.hlsl",
                  "Texture2D<float4> source:register(t1);\n"
                  "float4 VSMain(uint id:SV_VertexID):SV_Position{float2 "
                  "p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1);}\n"
                  "float4 PSMain(float4 p:SV_Position):SV_Target0{return "
                  "source.Load(int3(int2(p.xy),0));}\n");
    Check(!renderer.ReloadPostProcess(fixture.Path(L"unbound.hlsl"), diagnostics),
          "Unbound post t1 rejects only candidate");
    std::cout << "expected_post_root_rejection: " << diagnostics << '\n';
    Check(renderer.GetPostGeneration() == generation, "Root rejection preserves usable old PSO");
    fixture.Write(
        L"linkage.hlsl",
        "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};\n"
        "V VSMain(uint id:SV_VertexID){V o;o.p=float4(id,0,0,1);o.uv=float2(id,1);return o;}\n"
        "float4 PSMain(float4 p:SV_Position,float2 uv:TEXCOORD1):SV_Target0{return "
        "float4(uv,0,1);}\n");
    Check(!renderer.ReloadPostProcess(fixture.Path(L"linkage.hlsl"), diagnostics),
          "Post semantic mismatch rejects only candidate");
    std::cout << "expected_post_linkage_rejection: " << diagnostics << '\n';
    Check(renderer.GetPostGeneration() == generation, "Linkage rejection preserves generation");
    render();
    Compare(direct, renderer.CaptureRgba(), false);
    fixture.Write(L"replace.hlsl",
                  "Texture2D<float4> source:register(t0);\n"
                  "float4 VSMain(uint id:SV_VertexID):SV_Position{float2 "
                  "p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1);}\n"
                  "float4 PSMain(float4 p:SV_Position):SV_Target0{float4 "
                  "c=source.Load(int3(int2(p.xy),0));return float4(1-c.rgb,c.a);}\n");
    Check(renderer.ReloadPostProcess(fixture.Path(L"replace.hlsl"), diagnostics),
          "True post algorithm replacement accepted");
    Check(renderer.GetPostGeneration() == generation + 1,
          "Accepted replacement increments only post generation");
    render();
    const auto inverted = renderer.CaptureRgba();
    Check(std::abs(static_cast<int>(inverted[center]) - (255 - direct[center])) <= 1 &&
              inverted[center + 3] == direct[center + 3],
          "Actual new shader changes output while retaining alpha");
    Check(renderer.ReloadPostProcess(post, diagnostics),
          "Post recovery to original shader accepted");
    render();
    Compare(direct, renderer.CaptureRgba(), false);
    Check(renderer.GetTriangleGeometryInfo().upload_fence == upload,
          "Post/resize/capture do not repeat static vertex upload");
    renderer.Shutdown();
    const auto timings = renderer.TakeCompletedTimings();
    Check(timings.size() == submitted && renderer.GetDroppedTimingSamples() == 0,
          "Capture/reload commands do not invent GPU-frame samples; shutdown retains tail");
    for (std::size_t index = 0; index < timings.size(); ++index)
    {
        const auto& sample = timings[index];
        Check(sample.frame_number == index + 1 && sample.presentation == 0,
              "Offscreen has unique submission sequence and no presentation");
        Check(std::isfinite(sample.cpu_record_ms) && sample.cpu_record_ms >= 0 &&
                  std::isfinite(sample.gpu_pass_ms) && sample.gpu_pass_ms >= 0 &&
                  std::isfinite(sample.gpu_post_ms) && sample.gpu_post_ms >= 0,
              "Scene and post independent fence-completed measurements finite");
        Check(sample.postprocess != tinyme::PostProcess::None || sample.gpu_post_ms == 0,
              "Direct does not read unresolved post queries");
    }
    Check(timings[timings.size() - 2].post_generation == generation + 1 &&
              timings.back().post_generation == generation + 2 &&
              timings.back().pipeline_generation == 1,
          "Samples associate accepted post replacement independently from unchanged scene "
          "generation");
    Check(renderer.GetValidationStats().warnings == 0 && renderer.GetValidationStats().errors == 0,
          "Normal post/offscreen/capture/resize and safe teardown have no unexpected diagnostics");
    Reject([&] { static_cast<void>(renderer.CaptureRgba()); }, "Shutdown rejects capture");
    std::cout << "m3_pipeline submitted=" << submitted << " samples=" << timings.size()
              << " gpu_validation=" << gbv << '\n';
}
void TestWindow(bool gbv)
{
    const auto directory = tinyme::tests::ExecutableDirectory() / L"shaders";
    tinyme::Window window(L"TinyMeEngine M3 post lifecycle", {640, 360});
    tinyme::ClearRenderer renderer(window.GetHandle(), window.GetClientSize(),
                                   {true, gbv, true, tinyme::PostProcess::Copy});
    std::string diagnostics;
    Check(renderer.ReloadTriangle(directory / L"Triangle.hlsl", diagnostics),
          "Windowed indexed pipeline accepted");
    Check(renderer.ReloadPostProcess(directory / L"PostProcess.hlsl", diagnostics),
          "Windowed post pipeline accepted");
    for (unsigned index = 0; index < 4; ++index)
    {
        window.PumpMessages();
        Check(!window.IsCloseRequested(), "Windowed post still owns live message pump");
        Check(renderer.Render(kClear), "Windowed post presents valid output");
    }
    Reject([&] { static_cast<void>(renderer.CaptureRgba()); },
           "Window capture is explicitly unsupported not a synchronous Render readback");
    window.SetClientSize({641, 359});
    window.PumpMessages();
    Check(!window.IsCloseRequested(), "Window resize pump remains valid");
    renderer.Resize(window.GetClientSize());
    renderer.SetPostProcess(tinyme::PostProcess::Grayscale);
    for (unsigned index = 0; index < 3; ++index)
        Check(renderer.Render(kClear), "Windowed grayscale works after odd resize");
    Check(renderer.ReloadPostProcess(directory / L"PostProcess.hlsl", diagnostics),
          "Windowed F5 post rebuild succeeds");
    Check(renderer.Render(kClear), "Post F5 leaves windowed renderer usable");
    renderer.Shutdown();
    const auto samples = renderer.TakeCompletedTimings();
    Check(samples.size() == 8, "Windowed post shutdown preserves eight samples");
    for (std::size_t index = 0; index < samples.size(); ++index)
        Check(samples[index].presentation == index + 1 && samples[index].frame_number == index + 1,
              "Windowed post retains real presents and GPU frame sequence");
    Check(renderer.GetValidationStats().warnings == 0 && renderer.GetValidationStats().errors == 0,
          "Windowed post resources have no live children after shutdown");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        const bool gbv = argc == 2 && std::string_view(argv[1]) == "--gpu-validation";
        if (argc > 2 || (argc == 2 && !gbv))
            throw std::invalid_argument("Unknown GPU test argument");
        TestPipeline(gbv);
        TestWindow(gbv);
        std::cerr << "m3_gpu checks=" << checks << " failures=0 gpu_validation=" << gbv << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
