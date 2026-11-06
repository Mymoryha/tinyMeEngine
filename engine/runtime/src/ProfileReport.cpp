#include "ProfileReport.h"

#include <tinyme/core/TimingStatistics.h>
#include <tinyme/core/Version.h>

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tinyme
{
namespace
{
std::string Quote(std::string_view text)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : text)
    {
        if (character == '"' || character == '\\')
        {
            output << '\\' << static_cast<char>(character);
        }
        else if (character < 0x20)
        {
            output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(character) << std::dec;
        }
        else
        {
            output << static_cast<char>(character);
        }
    }
    output << '"';
    return output.str();
}
std::string Utf8Text(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument("Report text too long.");
    }
    const auto length = static_cast<int>(text.size());
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                                           nullptr, 0, nullptr, nullptr);
    if (bytes == 0)
    {
        throw std::invalid_argument("Invalid Unicode report text.");
    }
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, result.data(),
                            bytes, nullptr, nullptr) != bytes)
    {
        throw std::runtime_error("Cannot encode report text.");
    }
    return result;
}
std::string Utf8Path(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
void Metric(std::ostream& output, const TimingStatistics& stats)
{
    if (stats.count == 0)
    {
        output << "null";
        return;
    }
    output << "{\"count\":" << stats.count << ",\"mean_ms\":" << stats.mean_ms
           << ",\"median_ms\":" << stats.median_ms << ",\"p95_ms\":" << stats.p95_ms
           << ",\"min_ms\":" << stats.min_ms << ",\"max_ms\":" << stats.max_ms << '}';
}
} // namespace
void ResolveSessionShaderPaths(WindowedRunOptions& options)
{
    if ((!options.triangle || !options.shader_path.empty()) &&
        (options.postprocess == PostProcess::None || !options.post_shader_path.empty()))
    {
        return;
    }
    std::vector<wchar_t> executable(32768);
    const auto length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size())
    {
        throw std::runtime_error("Cannot locate default shader next to executable.");
    }
    const auto directory = std::filesystem::path(executable.data()).parent_path() / L"shaders";
    if (options.triangle && options.shader_path.empty())
    {
        options.shader_path = directory / L"Triangle.hlsl";
    }
    if (options.postprocess != PostProcess::None && options.post_shader_path.empty())
    {
        options.post_shader_path = directory / L"PostProcess.hlsl";
    }
}
void ValidateGpuSessionOptions(const WindowedRunOptions& options)
{
    if (!IsValid(options.parameters) || !IsValid(options.profile) || !IsValid(options.camera) ||
        !IsValid(options.postprocess) || options.width == 0 || options.height == 0 ||
        options.width > kMaxRenderDimension || options.height > kMaxRenderDimension ||
        options.frame_limit > 1'000'000 || (options.offscreen && options.frame_limit == 0))
    {
        throw std::invalid_argument("Invalid GPU session parameters, dimensions or frame budget.");
    }
    if (!options.offscreen && !options.capture_path.empty())
    {
        throw std::invalid_argument("Capture requires an offscreen GPU session.");
    }
    if (options.postprocess == PostProcess::None && !options.post_shader_path.empty())
    {
        throw std::invalid_argument("Post shader requires an enabled postprocess.");
    }
    if (options.profile.enabled && options.frame_limit != 0 &&
        options.frame_limit < options.profile.warmup_frames + options.profile.sample_frames)
    {
        throw std::invalid_argument("Frame budget cannot cover profile samples.");
    }
    if (!options.report_path.empty() && !options.profile.enabled)
    {
        throw std::invalid_argument("Report requires profiling.");
    }
    const auto validate_output = [](const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return;
        }
        const auto filename = path.filename().native();
        if (filename.empty() || filename.back() == L'.' || filename.back() == L' ' ||
            filename.find(L':') != std::wstring::npos)
        {
            throw std::invalid_argument("Output filename must not use trailing dot/space or ADS.");
        }
        auto parent = path.parent_path();
        if (parent.empty())
        {
            parent = std::filesystem::current_path();
        }
        if (!std::filesystem::is_directory(parent) ||
            std::filesystem::exists(std::filesystem::symlink_status(path)))
        {
            throw std::invalid_argument("Output parent must exist; expected a new output path.");
        }
    };
    validate_output(options.report_path);
    validate_output(options.capture_path);
    const auto same_path = [](const std::filesystem::path& left, const std::filesystem::path& right)
    {
        if (left.empty() || right.empty())
        {
            return false;
        }
        const auto a = std::filesystem::weakly_canonical(left).native();
        const auto b = std::filesystem::weakly_canonical(right).native();
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
    };
    if (same_path(options.report_path, options.capture_path) ||
        same_path(options.report_path, options.shader_path) ||
        same_path(options.report_path, options.post_shader_path) ||
        same_path(options.capture_path, options.shader_path) ||
        same_path(options.capture_path, options.post_shader_path))
    {
        throw std::invalid_argument("GPU session input and output paths conflict.");
    }
    if (options.offscreen)
    {
        const auto validate_source = [](const std::filesystem::path& path)
        {
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::invalid_argument("Shader source must be an existing regular file.");
            }
            const auto bytes = std::filesystem::file_size(path);
            if (bytes == 0 || bytes > 4 * 1024 * 1024)
            {
                throw std::invalid_argument("Shader source size must be in [1, 4 MiB].");
            }
        };
        if (options.triangle)
        {
            validate_source(options.shader_path);
        }
        if (options.postprocess != PostProcess::None)
        {
            validate_source(options.post_shader_path);
        }
    }
}
std::string BuildProfileReport(const WindowedRunOptions& options, const RendererInfo& info,
                               WindowSize initial_size, std::wstring_view application,
                               std::uint64_t generation, bool debug_layer, ProfileStatus status,
                               std::span<const FrameTiming> samples, TriangleGeometryInfo geometry,
                               std::uint64_t post_generation)
{
    if (!IsValid(options.camera) || !IsValid(options.postprocess) || initial_size.width == 0 ||
        initial_size.height == 0)
    {
        throw std::invalid_argument("Invalid profile camera, viewport or postprocess.");
    }
    const bool has_post = options.postprocess != PostProcess::None;
    std::vector<double> cpu, gpu, post;
    for (const auto& sample : samples)
    {
        cpu.push_back(sample.cpu_record_ms);
        gpu.push_back(sample.gpu_pass_ms);
        if (has_post)
        {
            post.push_back(sample.gpu_post_ms);
        }
    }
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::setprecision(9) << std::boolalpha;
    json << "{\n  \"schema\":1,\n  \"engine_version\":" << Quote(kVersion)
         << ",\n  \"application\":" << Quote(Utf8Text(application))
#ifdef _DEBUG
         << ",\n  \"build\":\"Debug\""
#else
         << ",\n  \"build\":\"RelWithDebInfo\""
#endif
         << ",\n  \"adapter\":" << Quote(info.adapter) << ",\n  \"driver\":" << Quote(info.driver)
         << ",\n  \"timestamp_frequency_hz\":" << info.timestamp_frequency
         << ",\n  \"debug_layer\":" << debug_layer
         << ",\n  \"gpu_validation\":" << options.gpu_validation
         << ",\n  \"mode\":" << Quote(options.offscreen ? "offscreen" : "windowed")
         << ",\n  \"window_created\":" << !options.offscreen
         << ",\n  \"vsync\":" << !options.offscreen
         << ",\n  \"format\":\"RGBA8_UNORM\",\n  \"experiment\":"
         << Quote(options.triangle ? "triangle" : "clear") << ",\n  \"shader_source\":"
         << Quote(options.triangle ? Utf8Path(options.shader_path) : "")
         << ",\n  \"shader_profile\":" << (options.triangle ? Quote("vs_6_0/ps_6_0") : "null")
         << ",\n  \"shader_options\":" << (options.triangle ? Quote("HLSL2021 O3 Ges WX") : "null")
         << ",\n  \"compiler\":" << (options.triangle ? Quote("DXC 1.8.2505.32") : "null")
         << ",\n  \"postprocess\":" << Quote(ToString(options.postprocess))
         << ",\n  \"post_shader_source\":"
         << (has_post ? Quote(Utf8Path(options.post_shader_path)) : "null")
         << ",\n  \"post_shader_profile\":" << (has_post ? Quote("vs_6_0/ps_6_0") : "null")
         << ",\n  \"post_shader_options\":" << (has_post ? Quote("HLSL2021 O3 Ges WX") : "null")
         << ",\n  \"post_compiler\":" << (has_post ? Quote("DXC 1.8.2505.32") : "null")
         << ",\n  \"post_generation\":" << post_generation
         << ",\n  \"pass_order\":" << (has_post ? "[\"scene\",\"postprocess\"]" : "[\"scene\"]")
         << ",\n  \"parameters_applied\":" << options.triangle
         << ",\n  \"width\":" << initial_size.width << ",\n  \"height\":" << initial_size.height
         << ",\n  \"clear_rgba\":[0.025,0.055,0.09,1],\n  \"scale\":" << options.parameters.scale
         << ",\n  \"tint\":[";
    for (std::size_t index = 0; index < 4; ++index)
    {
        if (index)
        {
            json << ',';
        }
        json << options.parameters.tint[index];
    }
    json << "],\n  \"parameter_bytes\":32,\n  \"geometry\":";
    if (options.triangle && geometry.index_count != 0)
    {
        json << "{\"kind\":\"fixed_indexed_triangle\",\"vertex_count\":" << geometry.vertex_count
             << ",\"index_count\":" << geometry.index_count
             << ",\"vertex_stride\":" << geometry.vertex_stride
             << ",\"index_format\":\"R16_UINT\",\"input_layout\":\"POSITION3_COLOR3\""
             << ",\"upload_fence\":" << geometry.upload_fence
             << ",\"last_use_fence\":" << geometry.last_use_fence << '}';
    }
    else
    {
        json << "null";
    }
    json << ",\n  \"camera\":";
    if (options.triangle)
    {
        const auto array = [&](std::span<const float> values)
        {
            json << '[';
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                if (index != 0)
                {
                    json << ',';
                }
                json << values[index];
            }
            json << ']';
        };
        json << "{\"eye\":";
        array(options.camera.eye);
        json << ",\"target\":";
        array(options.camera.target);
        json << ",\"up\":";
        array(options.camera.up);
        json << ",\"vertical_fov_degrees\":" << options.camera.vertical_fov_degrees
             << ",\"near_plane\":" << options.camera.near_plane
             << ",\"far_plane\":" << options.camera.far_plane
             << ",\"matrix_bytes\":64,\"convention\":\"LH row-major row-vector View*Projection; "
                "depth [0,1]\""
             << ",\"world_to_clip\":";
        array(BuildCameraTransform(options.camera, static_cast<float>(initial_size.width) /
                                                       static_cast<float>(initial_size.height))
                  .world_to_clip);
        json << '}';
    }
    else
    {
        json << "null";
    }
    json << ",\n  \"capture\":";
    if (!options.capture_path.empty())
    {
        json << "{\"path\":" << Quote(Utf8Path(options.capture_path))
             << ",\"width\":" << initial_size.width << ",\"height\":" << initial_size.height
             << ",\"format\":\"PNG\",\"pixel_format\":\"RGBA8_UNORM\",\"saved\":"
             << status.capture_saved << '}';
    }
    else
    {
        json << "null";
    }
    json << ",\n  \"pipeline_generation\":" << generation
         << ",\n  \"warmup_frames\":" << options.profile.warmup_frames
         << ",\n  \"requested_samples\":" << options.profile.sample_frames
         << ",\n  \"sample_count\":" << samples.size() << ",\n  \"complete\":" << status.complete
         << ",\n  \"environment_changed\":" << status.environment_changed
         << ",\n  \"closed_by_user\":" << status.closed_by_user
         << ",\n  \"validation_warnings\":" << status.validation_warnings
         << ",\n  \"validation_errors\":" << status.validation_errors
         << ",\n  \"dropped_samples\":" << status.dropped
         << ",\n  \"cpu_scope\":\"allocator reset through command-list close; includes scene and "
            "optional post command recording; excludes fence wait, submit, present, diagnostics, "
            "camera math, initialization upload and reload, capture/readback and PNG encoding\""
         << ",\n  \"gpu_scope\":\"clear plus optional indexed triangle; excludes barriers, query "
            "resolve, present, initialization upload and reload, postprocess and capture\""
         << ",\n  \"gpu_post_scope\":"
         << (has_post ? Quote("fullscreen postprocess draw only; excludes barriers, scene, query "
                              "resolve, present, reload and capture")
                      : "null")
         << ",\n  \"p95_method\":\"nearest rank\",\n  \"cpu_record\":";
    Metric(json, SummarizeTimings(cpu));
    json << ",\n  \"gpu_pass\":";
    Metric(json, SummarizeTimings(gpu));
    json << ",\n  \"gpu_post\":";
    Metric(json, SummarizeTimings(post));
    json << ",\n  \"samples\":[";
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        if (index)
        {
            json << ',';
        }
        const auto& sample = samples[index];
        json << "\n    {\"presentation\":" << sample.presentation
             << ",\"cpu_record_ms\":" << sample.cpu_record_ms
             << ",\"gpu_pass_ms\":" << sample.gpu_pass_ms
             << ",\"frame_number\":" << sample.frame_number
             << ",\"pipeline_generation\":" << sample.pipeline_generation
             << ",\"post_generation\":" << sample.post_generation
             << ",\"postprocess\":" << Quote(ToString(sample.postprocess)) << ",\"gpu_post_ms\":";
        if (has_post)
        {
            json << sample.gpu_post_ms;
        }
        else
        {
            json << "null";
        }
        json << '}';
    }
    json << "\n  ]\n}\n";
    return json.str();
}
void SaveProfileReport(const std::filesystem::path& path, std::string_view json)
{
    SaveProfileReport(path, json, {});
}
void SaveProfileReport(const std::filesystem::path& path, std::string_view json,
                       const ProfileFileOperations& operations)
{
    if (json.size() > std::numeric_limits<DWORD>::max())
    {
        throw std::runtime_error("Report too large.");
    }
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error(
            "Cannot create new report; parent must exist and output must not exist. Win32=" +
            std::to_string(GetLastError()));
    }
    DWORD written = 0;
    const BOOL success =
        operations.write(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (success && written != json.size())
    {
        error = ERROR_WRITE_FAULT;
    }
    if (error == ERROR_SUCCESS && !operations.flush(file))
    {
        error = GetLastError();
    }
    DWORD cleanup_error = ERROR_SUCCESS;
    const auto mark_for_deletion = [&]()
    {
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!operations.dispose(file, FileDispositionInfo, &disposition, sizeof(disposition)))
        {
            cleanup_error = GetLastError();
        }
    };
    if (error != ERROR_SUCCESS)
    {
        mark_for_deletion();
    }
    if (!operations.close(file))
    {
        const DWORD close_error = GetLastError();
        if (error == ERROR_SUCCESS)
        {
            error = close_error;
            mark_for_deletion();
        }
        if (!operations.close(file))
        {
            cleanup_error = GetLastError();
        }
    }
    if (error != ERROR_SUCCESS)
    {
        throw std::runtime_error("Report write failed: " + std::to_string(error) +
                                 "; cleanup error=" + std::to_string(cleanup_error));
    }
}
} // namespace tinyme
