#include "gt_esmini/core/ConfigLoader.hpp"

namespace gt_esmini
{
std::string ConfigLoader::ResolveConfigPath(const std::string& exe_dir, const std::string& filename) const
{
    return exe_dir + "/config/" + filename;
}
} // namespace gt_esmini
