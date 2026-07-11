#include "gt_esmini/core/ConfigLoader.hpp"

namespace gt_esmini
{
std::string ConfigLoader::ResolveConfigPath(const std::string& exe_dir, const std::string& filename) const
{
    // Config lives at <parent of exe_dir>/config/ (sibling of bin/)
    // e.g. exe_dir = ".../bin/" → ".../config/filename"
    //      exe_dir = ".../build/GT_esmini/Release/" → ".../build/GT_esmini/config/filename"
    // exe_dir is the directory of *this module* (GT_esminiLib.dll / GT_Sim.exe),
    // not the host process — see GetCurrentModuleDirectory (audit F5).
    return exe_dir + "/../config/" + filename;
}

bool ConfigLoader::IsAbsolutePath(const std::string& path)
{
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;               // POSIX / UNC
    if (path.size() > 1 && path[1] == ':') return true;               // Windows drive (C:\...)
    return false;
}

std::string ConfigLoader::ResolveConfigPathOrPassthrough(const std::string& exe_dir, const std::string& filename) const
{
    if (IsAbsolutePath(filename)) return filename;
    return ResolveConfigPath(exe_dir, filename);
}
} // namespace gt_esmini
