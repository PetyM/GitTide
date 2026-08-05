#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "gittide/giterror.hpp"

namespace gittide {

/// Tuning for scanForRepos.
struct ScanOptions
{
    /// Directory levels below the root to search; 1 = its immediate children.
    /// Values below 1 are clamped to 1.
    int maxDepth = 2;
};

/// Find the git repositories under `root`.
///
/// Descent stops at a repository — a repository's interior is never searched,
/// so **submodules and nested checkouts are never returned**: submodules reach
/// the user through the parent repository's submodule tree, and returning them
/// here would duplicate each one as a top-level repository.
/// Directories whose name begins with '.' are skipped, as are directories that
/// cannot be read (a permission error is not a scan failure). When `root` is
/// itself a repository it is the sole result.
///
/// @returns repository paths as generic UTF-8 (forward slashes), sorted and
/// deduplicated; an empty vector when the tree holds none.
/// @returns an error only when `root` does not exist or is not a directory.
Expected<std::vector<std::string>> scanForRepos(const std::filesystem::path& root, ScanOptions opt = {});

} // namespace gittide
