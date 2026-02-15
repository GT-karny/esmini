#pragma once

#include <string>

namespace gt_esmini
{
class IConfigLoader
{
public:
    virtual ~IConfigLoader() = default;
    virtual std::string ResolveConfigPath(const std::string& exe_dir, const std::string& filename) const = 0;
};
} // namespace gt_esmini
