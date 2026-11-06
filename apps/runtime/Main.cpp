#include "Application.h"

int wmain(int argc, wchar_t* argv[])
{
    return tinyme::RunApplication(tinyme::ApplicationKind::Runtime, argc, argv);
}
