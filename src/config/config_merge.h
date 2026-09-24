#pragma once

#include "config/config_diag.h"
#include "core/toml.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace umbriel::configmerge {

  struct MergeResult {
    toml::table merged;
    std::map<std::string, toml::source_region> effectOwners;
    std::vector<std::filesystem::path> loadedFiles;
    std::vector<ConfigDiagnostic> diagnostics;
    bool hadError = false;
    bool missingIncludes = false;
    bool missingOptionalIncludes = false;
  };

  [[nodiscard]] MergeResult mergeWithIncludes(const std::filesystem::path& rootFile);
  void deepMerge(toml::table& base, toml::table&& overlay);

} // namespace umbriel::configmerge
