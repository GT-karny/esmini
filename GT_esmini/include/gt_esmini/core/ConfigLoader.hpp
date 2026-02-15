#pragma once

#include "gt_esmini/core/IConfigLoader.hpp"

namespace gt_esmini
{
class ConfigLoader final : public IConfigLoader
{
public:
    std::string ResolveConfigPath(const std::string& exe_dir, const std::string& filename) const override;
};
} // namespace gt_esmini
