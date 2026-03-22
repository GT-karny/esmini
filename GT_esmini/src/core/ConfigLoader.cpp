#include "gt_esmini/core/ConfigLoader.hpp"

namespace gt_esmini
{
std::string ConfigLoader::ResolveConfigPath(const std::string& exe_dir, const std::string& filename) const
{
    // Config lives at <parent of exe_dir>/config/ (sibling of bin/)
    // e.g. exe_dir = ".../bin/" → ".../config/filename"
    //      exe_dir = ".../build/GT_esmini/Release/" → ".../build/GT_esmini/config/filename"
    return exe_dir + "/../config/" + filename;
}
} // namespace gt_esmini
