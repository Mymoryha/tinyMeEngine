#pragma once

#include <tinyme/platform/windows/Window.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tinyme::tests
{
inline std::filesystem::path ExecutableDirectory()
{
    std::vector<wchar_t> name(32768);
    const auto count = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    if (count == 0 || count >= name.size())
    {
        throw std::runtime_error("Test executable path unavailable.");
    }
    return std::filesystem::path(name.data()).parent_path();
}

class ShaderFixture
{
  public:
    ShaderFixture()
        : directory_(ExecutableDirectory() / L"test-scratch" /
                     std::to_wstring(GetCurrentProcessId()))
    {
        std::filesystem::create_directories(directory_);
    }
    ~ShaderFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }
    ShaderFixture(const ShaderFixture&) = delete;
    ShaderFixture& operator=(const ShaderFixture&) = delete;
    [[nodiscard]] std::filesystem::path Path(std::wstring_view name) const
    {
        return directory_ / name;
    }
    void Write(std::wstring_view name, std::string_view text) const
    {
        std::ofstream file(Path(name), std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file)
        {
            throw std::runtime_error("Cannot write test shader fixture.");
        }
    }

  private:
    std::filesystem::path directory_;
};
} // namespace tinyme::tests
