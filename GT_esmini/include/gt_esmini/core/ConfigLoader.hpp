#pragma once

#include "gt_esmini/core/IConfigLoader.hpp"

namespace gt_esmini
{
class ConfigLoader final : public IConfigLoader
{
public:
    // Resolve a bare config filename to <exe_dir>/../config/<filename>.
    std::string ResolveConfigPath(const std::string& exe_dir, const std::string& filename) const override;

    // True if `path` is an absolute path (POSIX "/..." or Windows "X:...").
    static bool IsAbsolutePath(const std::string& path);

    // Absolute paths pass through unchanged; bare filenames resolve via
    // ResolveConfigPath(). This is the single decision point controllers use so
    // relative/normal placement and the absolute-path ConfigFile injection
    // (web backend per-run config) share one rule.
    std::string ResolveConfigPathOrPassthrough(const std::string& exe_dir, const std::string& filename) const;
};
} // namespace gt_esmini
