#pragma once
#include <filesystem>
#include <vector>

namespace gittide {

/// The set of filesystem paths to watch in order to keep a repository's view
/// current. Computed by GitRepo::watchTargets(). Pure std — no Qt, no libgit2.
///
/// @c dirs is a flat list of *directories* to hand to a filesystem watcher: every
/// non-ignored working-tree directory plus every directory inside the git dir.
/// Watching directories (not individual files) is portable and scales: it catches
/// files added/removed/renamed in the tree and git's atomic @c *.lock → rename
/// rewrites of @c index / @c HEAD / @c refs / @c MERGE_HEAD / the rebase dirs,
/// because each lands in a watched directory. @c workdir and @c gitDir let the
/// watcher classify a changed path: a path under @c gitDir means git state moved
/// (full refresh), otherwise only the working tree changed (status refresh).
struct WatchTargets
{
    std::filesystem::path              workdir; ///< working-tree root (empty for a bare repo)
    std::filesystem::path              gitDir;  ///< the .git directory
    std::vector<std::filesystem::path> dirs;    ///< all directories to watch
};

} // namespace gittide
