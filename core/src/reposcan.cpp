#include "gittide/reposcan.hpp"

#include <algorithm>
#include <system_error>

#include "gittide/gitrepo.hpp"
#include "gittide/pathutil.hpp"

namespace gittide {
namespace {

/// Depth-first walk. `depth` counts levels already descended below the root.
void walk(const std::filesystem::path& dir, int depth, int maxDepth, std::vector<std::string>& out)
{
    if (depth >= maxDepth)
        return;

    std::error_code ec;
    std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
        return; // unreadable directory: skipped, not a scan failure

    for (const auto& entry : it)
    {
        if (!entry.is_directory(ec) || ec)
            continue;
        const std::string name = toGitPath(entry.path().filename());
        if (name.empty() || name.front() == '.')
            continue;

        if (GitRepo::open(entry.path()))
        {
            out.push_back(toGitPath(entry.path()));
            // A repository terminates the descent: its submodules and nested
            // checkouts must never be offered as repositories of their own.
            continue;
        }
        walk(entry.path(), depth + 1, maxDepth, out);
    }
}

} // namespace

Expected<std::vector<std::string>> scanForRepos(const std::filesystem::path& root, ScanOptions opt)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
        return std::unexpected(GitError{-1, "not a directory: " + toGitPath(root)});

    std::vector<std::string> out;
    if (GitRepo::open(root))
    {
        out.push_back(toGitPath(root));
        return out;
    }

    walk(root, 0, std::max(1, opt.maxDepth), out);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace gittide
