#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tinyme
{
struct ShaderCompileResult
{
    std::vector<unsigned char> bytecode;
    std::string diagnostics;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return !bytecode.empty();
    }
};

[[nodiscard]] std::string ReadShaderSource(const std::filesystem::path& source,
                                           std::string& diagnostics);
// Both stages consume the same main-source snapshot. Includes are read at compilation time.
// Loads the pinned compiler next to the executable, never from PATH/current directory.
[[nodiscard]] ShaderCompileResult CompileShader(const std::filesystem::path& source,
                                                std::string_view text, std::wstring_view entry,
                                                std::wstring_view profile);
} // namespace tinyme
