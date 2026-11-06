#pragma once

namespace tinyme
{
enum class ApplicationKind
{
    Runtime,
    Editor
};

int RunApplication(ApplicationKind kind, int argc, wchar_t* argv[]);
} // namespace tinyme
