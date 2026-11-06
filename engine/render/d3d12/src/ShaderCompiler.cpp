#include "ShaderCompiler.h"

#include <objbase.h>
#include <tinyme/platform/windows/Window.h>

#include <dxcapi.h>
#include <wrl/client.h>

#include <exception>
#include <fstream>
#include <stdexcept>

namespace tinyme
{
namespace
{
void CheckCompiler(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
    }
}

struct CompilerModule
{
    HMODULE handle = nullptr;
    ~CompilerModule()
    {
        if (handle)
        {
            FreeLibrary(handle);
        }
    }
};
} // namespace

std::string ReadShaderSource(const std::filesystem::path& source, std::string& diagnostics)
{
    diagnostics.clear();
    std::ifstream file(source, std::ios::binary);
    if (!file)
    {
        const auto path = source.generic_u8string();
        diagnostics = "Cannot open shader: " +
                      std::string(reinterpret_cast<const char*>(path.data()), path.size());
        return {};
    }
    constexpr std::size_t kMaxShaderBytes = 4 * 1024 * 1024;
    std::string text(kMaxShaderBytes + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (file.bad() || text.empty() || text.size() > kMaxShaderBytes)
    {
        diagnostics = "Shader is empty, unreadable, or exceeds 4 MiB.";
        return {};
    }
    return text;
}

ShaderCompileResult CompileShader(const std::filesystem::path& source, std::string_view text,
                                  std::wstring_view entry, std::wstring_view profile)
{
    ShaderCompileResult output;
    try
    {
        std::vector<wchar_t> executable(32768);
        const auto length =
            GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size())
        {
            throw std::runtime_error("Cannot locate executable for pinned DXC.");
        }
        const auto dll = std::filesystem::path(executable.data()).parent_path() / L"dxcompiler.dll";
        CompilerModule module;
        module.handle = LoadLibraryExW(
            dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module.handle)
        {
            throw std::runtime_error("Cannot load pinned dxcompiler.dll next to executable.");
        }
        const auto create = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(module.handle, "DxcCreateInstance"));
        if (!create)
        {
            throw std::runtime_error("DXC factory is missing.");
        }
        Microsoft::WRL::ComPtr<IDxcUtils> utils;
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
        CheckCompiler(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils)), "DXC utils");
        CheckCompiler(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)), "DXC compiler");
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> includes;
        CheckCompiler(utils->CreateDefaultIncludeHandler(&includes), "DXC include handler");
        const auto filename = source.wstring();
        const auto directory = std::filesystem::absolute(source).parent_path().wstring();
        const std::wstring owned_entry(entry), owned_profile(profile);
        const wchar_t* arguments[]{filename.c_str(),
                                   L"-E",
                                   owned_entry.c_str(),
                                   L"-T",
                                   owned_profile.c_str(),
                                   L"-HV",
                                   L"2021",
                                   L"-Ges",
                                   L"-WX",
                                   L"-O3",
                                   L"-I",
                                   directory.c_str()};
        DxcBuffer buffer{text.data(), text.size(), DXC_CP_UTF8};
        Microsoft::WRL::ComPtr<IDxcResult> result;
        CheckCompiler(compiler->Compile(&buffer, arguments,
                                        static_cast<UINT32>(std::size(arguments)), includes.Get(),
                                        IID_PPV_ARGS(&result)),
                      "DXC Compile");
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> diagnostics;
        CheckCompiler(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnostics), nullptr),
                      "DXC diagnostics");
        if (diagnostics && diagnostics->GetStringLength())
        {
            output.diagnostics.assign(diagnostics->GetStringPointer(),
                                      diagnostics->GetStringLength());
        }
        HRESULT status = E_FAIL;
        CheckCompiler(result->GetStatus(&status), "DXC status");
        if (FAILED(status))
        {
            if (output.diagnostics.empty())
            {
                output.diagnostics = "DXC compilation failed without diagnostics.";
            }
            return output;
        }
        Microsoft::WRL::ComPtr<IDxcBlob> object;
        CheckCompiler(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr),
                      "DXC object");
        const auto* begin = static_cast<const unsigned char*>(object->GetBufferPointer());
        output.bytecode.assign(begin, begin + object->GetBufferSize());
    }
    catch (const std::exception& error)
    {
        output.bytecode.clear();
        output.diagnostics = error.what();
    }
    return output;
}
} // namespace tinyme
