#include "gt_esmini/control/common/ModuleDirectory.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace gt_esmini
{

std::string GetCurrentModuleDirectory()
{
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buffer, MAX_PATH) != 0)
    {
        std::string path(buffer);
        const size_t last_slash = path.find_last_of("\\/");
        if (last_slash != std::string::npos)
        {
            return path.substr(0, last_slash);
        }
    }
#else
    char buffer[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0)
    {
        buffer[len] = '\0';
        std::string path(buffer);
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
