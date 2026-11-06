#include "ShaderCompiler.h"
#include "ShaderFixture.h"

#include <iostream>
#include <stdexcept>

namespace
{
int checks = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    try
    {
        tinyme::tests::ShaderFixture fixture;
        const auto source = tinyme::tests::ExecutableDirectory() / L"shaders/Triangle.hlsl";
        std::string diagnostics;
        const auto snapshot = tinyme::ReadShaderSource(source, diagnostics);
        Check(!snapshot.empty() && diagnostics.empty(), "Default source read");
        const auto vs = tinyme::CompileShader(source, snapshot, L"VSMain", L"vs_6_0");
        const auto ps = tinyme::CompileShader(source, snapshot, L"PSMain", L"ps_6_0");
        Check(vs.IsValid() && vs.diagnostics.empty(), "Default vertex compilation");
        Check(ps.IsValid() && ps.diagnostics.empty(), "Default pixel compilation");
        const auto missing_entry =
            tinyme::CompileShader(source, snapshot, L"DoesNotExist", L"ps_6_0");
        Check(!missing_entry.IsValid() && !missing_entry.diagnostics.empty(),
              "Missing entry rejected with diagnosis");
        const auto syntax = tinyme::CompileShader(source, "invalid HLSL !", L"VSMain", L"vs_6_0");
        Check(!syntax.IsValid() && syntax.diagnostics.find("error") != std::string::npos,
              "Syntax failure diagnostics");
        Check(tinyme::ReadShaderSource(fixture.Path(L"missing.hlsl"), diagnostics).empty() &&
                  !diagnostics.empty(),
              "Missing file diagnosed");
        fixture.Write(L"empty.hlsl", "");
        Check(tinyme::ReadShaderSource(fixture.Path(L"empty.hlsl"), diagnostics).empty() &&
                  !diagnostics.empty(),
              "Empty file diagnosed");
        fixture.Write(L"large.hlsl", std::string(4 * 1024 * 1024 + 1, 'x'));
        Check(tinyme::ReadShaderSource(fixture.Path(L"large.hlsl"), diagnostics).empty() &&
                  !diagnostics.empty(),
              "Source budget enforced");
        fixture.Write(L"color.hlsli", "float4 Color() { return float4(0.2,0.7,0.9,1); }\n");
        fixture.Write(
            L"include.hlsl",
            "#include \"color.hlsli\"\nfloat4 PSMain() : SV_Target0 { return Color(); }\n");
        const auto include_path = fixture.Path(L"include.hlsl");
        const auto include_text = tinyme::ReadShaderSource(include_path, diagnostics);
        const auto include_result =
            tinyme::CompileShader(include_path, include_text, L"PSMain", L"ps_6_0");
        Check(include_result.IsValid(), "Relative include compilation");
        fixture.Write(L"color.hlsli", "syntax error");
        const auto broken_include =
            tinyme::CompileShader(include_path, include_text, L"PSMain", L"ps_6_0");
        Check(!broken_include.IsValid() &&
                  broken_include.diagnostics.find("color.hlsli") != std::string::npos,
              "Include failure retains file location");
        const auto recovered = tinyme::CompileShader(source, snapshot, L"PSMain", L"ps_6_0");
        Check(recovered.IsValid(), "Compiler works after failures");
        std::cout << "shader_compiler checks=" << checks << " failures=0 gpu_initialized=0\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: Shader compiler test: " << error.what() << '\n';
        return 1;
    }
}
