#include "ProfileReport.h"
#include "ShaderFixture.h"

#include <array>
#include <barrier>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
int checks = 0;
void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

template <typename Function> void CheckRejected(Function operation, const char* description)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, description);
}

std::string ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

enum class IoFailure
{
    Write,
    ShortWrite,
    Flush,
    Close,
    Disposition,
    RepeatedClose
};
struct IoFailureState
{
    IoFailure failure = IoFailure::Write;
    unsigned close_calls = 0;
    unsigned dispose_calls = 0;
    HANDLE open_handle = INVALID_HANDLE_VALUE;
    ~IoFailureState()
    {
        if (open_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(open_handle);
        }
    }
};
thread_local IoFailureState* io_failure = nullptr;
BOOL WINAPI WriteWithFailure(HANDLE file, LPCVOID bytes, DWORD size, LPDWORD written,
                             LPOVERLAPPED overlapped)
{
    if (io_failure->failure == IoFailure::Write || io_failure->failure == IoFailure::Disposition)
    {
        SetLastError(ERROR_DISK_FULL);
        return FALSE;
    }
    if (io_failure->failure == IoFailure::ShortWrite)
    {
        return WriteFile(file, bytes, size / 2, written, overlapped);
    }
    return WriteFile(file, bytes, size, written, overlapped);
}
BOOL WINAPI FlushWithFailure(HANDLE file)
{
    if (io_failure->failure == IoFailure::Flush)
    {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return FlushFileBuffers(file);
}
BOOL WINAPI DisposeWithFailure(HANDLE file, FILE_INFO_BY_HANDLE_CLASS info_class, LPVOID info,
                               DWORD size)
{
    ++io_failure->dispose_calls;
    if (io_failure->failure == IoFailure::Disposition)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return SetFileInformationByHandle(file, info_class, info, size);
}
BOOL WINAPI CloseWithFailure(HANDLE file)
{
    ++io_failure->close_calls;
    const unsigned failures = io_failure->failure == IoFailure::RepeatedClose ? 2u
                              : io_failure->failure == IoFailure::Close       ? 1u
                                                                              : 0u;
    if (io_failure->close_calls <= failures)
    {
        io_failure->open_handle = file;
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const BOOL closed = CloseHandle(file);
    if (closed)
    {
        io_failure->open_handle = INVALID_HANDLE_VALUE;
    }
    return closed;
}
void TestReportIoFailures()
{
    tinyme::tests::ShaderFixture fixture;
    const tinyme::ProfileFileOperations operations{&WriteWithFailure, &FlushWithFailure,
                                                   &DisposeWithFailure, &CloseWithFailure};
    const std::array failures{IoFailure::Write, IoFailure::ShortWrite,  IoFailure::Flush,
                              IoFailure::Close, IoFailure::Disposition, IoFailure::RepeatedClose};
    for (std::size_t index = 0; index < failures.size(); ++index)
    {
        IoFailureState state;
        state.failure = failures[index];
        io_failure = &state;
        const auto path = fixture.Path(std::to_wstring(index) + L"-failure.json");
        std::string diagnostic;
        try
        {
            tinyme::SaveProfileReport(path, "{\"complete\":true}\n", operations);
        }
        catch (const std::runtime_error& error)
        {
            diagnostic = error.what();
        }
        io_failure = nullptr;
        const DWORD primary_error =
            state.failure == IoFailure::Write || state.failure == IoFailure::Disposition
                ? ERROR_DISK_FULL
            : state.failure == IoFailure::Close || state.failure == IoFailure::RepeatedClose
                ? ERROR_INVALID_HANDLE
                : ERROR_WRITE_FAULT;
        const DWORD cleanup_error = state.failure == IoFailure::Disposition ? ERROR_ACCESS_DENIED
                                    : state.failure == IoFailure::RepeatedClose
                                        ? ERROR_INVALID_HANDLE
                                        : ERROR_SUCCESS;
        Check(diagnostic == "Report write failed: " + std::to_string(primary_error) +
                                "; cleanup error=" + std::to_string(cleanup_error),
              "Original I/O failure and final cleanup outcome diagnosed");
        Check(state.dispose_calls == 1,
              "Every I/O failure marks only its owned handle for deletion");
        Check(state.close_calls ==
                  (state.failure == IoFailure::Close || state.failure == IoFailure::RepeatedClose
                       ? 2u
                       : 1u),
              "Close failure triggers one bounded close retry");
        if (state.failure == IoFailure::Disposition)
        {
            Check(std::filesystem::exists(path),
                  "Failed deletion is not reported as successful cleanup");
            Check(std::filesystem::remove(path),
                  "Failed-deletion fixture removed after diagnostic check");
        }
        else if (state.failure == IoFailure::RepeatedClose)
        {
            Check(state.open_handle != INVALID_HANDLE_VALUE,
                  "Persistent close failure remains explicit");
            Check(CloseHandle(state.open_handle) != FALSE,
                  "Fault-injected handle released by test");
            state.open_handle = INVALID_HANDLE_VALUE;
        }
        else
        {
            Check(state.open_handle == INVALID_HANDLE_VALUE,
                  "Recoverable I/O failure leaves no open handle");
        }
        Check(!std::filesystem::exists(path), "Cleaned report failure leaves no partial output");
        tinyme::SaveProfileReport(path, "retry");
        Check(ReadBytes(path) == "retry", "Same output path can be retried after failed write");
    }
}
void TestConcurrentReports()
{
    tinyme::tests::ShaderFixture fixture;
    for (unsigned iteration = 0; iteration < 8; ++iteration)
    {
        const auto path = fixture.Path(std::to_wstring(iteration) + L"-race.json");
        const std::array payloads{std::string(4096, 'a'), std::string(4096, 'b')};
        std::array<bool, 2> succeeded{};
        std::barrier start(2);
        std::array<std::jthread, 2> writers;
        for (std::size_t index = 0; index < writers.size(); ++index)
        {
            writers[index] = std::jthread(
                [&, index]()
                {
                    start.arrive_and_wait();
                    try
                    {
                        tinyme::SaveProfileReport(path, payloads[index]);
                        succeeded[index] = true;
                    }
                    catch (const std::runtime_error&)
                    {
                    }
                });
        }
        for (auto& writer : writers)
        {
            writer.join();
        }
        Check(succeeded[0] != succeeded[1], "Exactly one concurrent CREATE_NEW writer succeeds");
        Check(ReadBytes(path) == payloads[succeeded[0] ? 0 : 1],
              "Losing writer cannot truncate, replace or delete winner's report");
    }
}

void TestReport()
{
    tinyme::WindowedRunOptions options;
    options.profile = {true, 0, 2};
    options.shader_path = std::filesystem::path(u8"shaders/测试.hlsl");
    tinyme::RendererInfo info{"test \"adapter\"\n", "driver\\test", 1000000};
    const std::array<tinyme::FrameTiming, 2> samples{
        tinyme::FrameTiming{1, 1, {640, 360}, 1.0, 2.0},
        tinyme::FrameTiming{2, 1, {640, 360}, 3.0, 4.0}};
    const auto json = tinyme::BuildProfileReport(options, info, {640, 360}, L"test\\name", 1, true,
                                                 {true, false, false, 0, 0}, samples,
                                                 {4, 3, 24, 2, 8, false, false});
    Check(json.find("\"geometry\":{\"kind\":\"fixed_indexed_triangle\",\"vertex_count\":4,\"index_"
                    "count\":3") != std::string::npos &&
              json.find("\"vertex_stride\":24") != std::string::npos &&
              json.find("\"index_format\":\"R16_UINT\"") != std::string::npos,
          "Actual indexed geometry snapshot serialized");
    Check(json.find("\"eye\":[0,0,-2]") != std::string::npos &&
              json.find("\"vertical_fov_degrees\":90") != std::string::npos &&
              json.find("\"matrix_bytes\":64") != std::string::npos &&
              json.find("\"world_to_clip\":[") != std::string::npos &&
              json.find("\"parameter_bytes\":32") != std::string::npos,
          "Camera matrix and original b0 parameters are separately documented");
    Check(json.find("\"application\":\"test\\\\name\"") != std::string::npos,
          "Application title is text, not a filesystem path");
    Check(json.find("\"complete\":true") != std::string::npos, "Complete report flag");
    Check(json.find("\"mean_ms\":2") != std::string::npos &&
              json.find("\"mean_ms\":3") != std::string::npos,
          "CPU and GPU are independently summarized");
    Check(json.find("test \\\"adapter\\\"\\u000a") != std::string::npos &&
              json.find("driver\\\\test") != std::string::npos,
          "JSON string and control characters escaped");
    Check(json.find("shaders/测试.hlsl") != std::string::npos, "UTF-8 shader path preserved");
    Check(json.find("\"closed_by_user\":false") != std::string::npos &&
              json.find("\"validation_warnings\":0") != std::string::npos,
          "Integrity reasons recorded");
    const auto empty = tinyme::BuildProfileReport(options, info, {640, 360}, L"test", 1, true,
                                                  {false, true, true, 2, 3}, {});
    Check(empty.find("\"cpu_record\":null") != std::string::npos &&
              empty.find("\"gpu_pass\":null") != std::string::npos &&
              empty.find("\"sample_count\":0") != std::string::npos,
          "Empty/incomplete report never invents zero timing statistics");
    Check(empty.find("\"complete\":false") != std::string::npos &&
              empty.find("\"closed_by_user\":true") != std::string::npos &&
              empty.find("\"environment_changed\":true") != std::string::npos &&
              empty.find("\"dropped_samples\":2") != std::string::npos &&
              empty.find("\"validation_warnings\":3") != std::string::npos,
          "Incomplete reasons survive serialization");
    const std::array<tinyme::FrameTiming, 1> invalid{
        tinyme::FrameTiming{1, 1, {640, 360}, std::numeric_limits<double>::quiet_NaN(), 0}};
    CheckRejected(
        [&]()
        {
            static_cast<void>(tinyme::BuildProfileReport(options, info, {640, 360}, L"test", 1,
                                                         true, {}, invalid));
        },
        "Nonfinite timing cannot become invalid JSON");

    auto clear_options = options;
    clear_options.triangle = false;
    const auto clear_json =
        tinyme::BuildProfileReport(clear_options, info, {640, 360}, L"test", 0, true, {}, samples);
    Check(clear_json.find("\"geometry\":null") != std::string::npos &&
              clear_json.find("\"camera\":null") != std::string::npos,
          "Clear never claims geometry or camera was applied");
    auto invalid_camera = options;
    invalid_camera.camera.eye = invalid_camera.camera.target;
    CheckRejected([&]()
                  { static_cast<void>(tinyme::RunWindowedSession(L"invalid", invalid_camera)); },
                  "Invalid session camera rejected before window/GPU");
    CheckRejected(
        [&]()
        {
            static_cast<void>(tinyme::BuildProfileReport(invalid_camera, info, {640, 360}, L"test",
                                                         0, true, {}, samples));
        },
        "Invalid camera cannot be serialized as valid report");
    tinyme::tests::ShaderFixture fixture;
    const auto path = fixture.Path(L"报告.json");
    tinyme::SaveProfileReport(path, json);
    std::ifstream file(path, std::ios::binary);
    const std::string saved{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();
    Check(saved == json, "Report bytes survive UTF-8/Unicode output path");
    CheckRejected([&]() { tinyme::SaveProfileReport(path, "replacement"); },
                  "Existing report never overwritten");
    Check(ReadBytes(path) == json, "Existing report bytes unchanged");
    CheckRejected([&]() { tinyme::SaveProfileReport(fixture.Path(L"missing/report.json"), json); },
                  "Missing parent directory rejected");

    options.parameters.scale = 0;
    CheckRejected([&]() { static_cast<void>(tinyme::RunWindowedSession(L"invalid", options)); },
                  "Invalid session parameters rejected without creating window/GPU");
    options.parameters = {};
    options.frame_limit = 1;
    CheckRejected([&]() { static_cast<void>(tinyme::RunWindowedSession(L"invalid", options)); },
                  "Insufficient programmatic frame budget rejected before GPU");
    options.frame_limit = 2;
    options.report_path = path;
    CheckRejected([&]() { static_cast<void>(tinyme::RunWindowedSession(L"invalid", options)); },
                  "Existing session report rejected before GPU");
}
} // namespace

int main()
{
    try
    {
        TestReport();
        TestReportIoFailures();
        TestConcurrentReports();
        std::cout << "profile_contracts checks=" << checks << " failures=0 no_gpu=1\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
