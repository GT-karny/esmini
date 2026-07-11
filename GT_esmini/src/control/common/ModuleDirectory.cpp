#include "gt_esmini/control/common/ModuleDirectory.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#endif

namespace gt_esmini
{

// Marker whose address lives inside *this* module (GT_esminiLib.dll / .so, or
// GT_Sim.exe when linked statically). We resolve the containing module from this
// address so we get the directory of the binary that owns this code — NOT the
// host process. This matters when GT_esminiLib is loaded in-process from
// python.exe (gt_sim_test, web backend): GetModuleFileNameA(nullptr, ...) would
// return the interpreter's directory, so config/resource resolution (which does
// "<module_dir>/../config/") pointed at a non-existent path and silently fell
// back to built-in defaults — e.g. policy_*_enabled in a relative-path config
// never took effect (audit F5).
namespace
{
void ModuleAnchor() {}
}  // namespace

std::string GetCurrentModuleDirectory()
{
#ifdef _WIN32
    char    buffer[MAX_PATH];
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&ModuleAnchor),
                           &module) &&
        GetModuleFileNameA(module, buffer, MAX_PATH) != 0)
    {
        std::string path(buffer);
        const size_t last_slash = path.find_last_of("\\/");
        if (last_slash != std::string::npos)
        {
            return path.substr(0, last_slash);
        }
    }
#else
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&ModuleAnchor), &info) != 0 && info.dli_fname != nullptr)
    {
        std::string path(info.dli_fname);
        const size_t last_slash = path.find_last_of('/');
        if (last_slash != std::string::npos)
        {
            return path.substr(0, last_slash);
        }
    }
#endif
    return ".";
}

}  // namespace gt_esmini
