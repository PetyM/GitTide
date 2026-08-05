#include "gittide/reposcan.hpp"

#include <algorithm>
#include <system_error>

#include "gittide/gitrepo.hpp"
#include "gittide/pathutil.hpp"

namespace gittide {
namespace {

// Cheap filesystem probe for "this directory might be a repository", checked
// before paying for a full GitRepo::open() — which logs a Warning on every
// failure, so calling it unconditionally on every ordinary subdirectory a
// scan visits would drown real warnings once an app wires a LogBackend. A
// normal checkout or a linked worktree/submodule has a `.git` entry (a
// directory for the former, a file for the latter); a bare repository has
// HEAD, objects and refs directly under it. GitRepo::open() stays the
// authoritative check, so a false positive here (e.g. a stray `.git` file
// that isn't a valid gitlink) is still rejected — it only costs one avoidable
// open attempt.
bool looksLikeRepo(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (std::filesystem::exists(dir / ".git", ec))
        return true;
    return std::filesystem::is_regular_file(dir / "HEAD", ec) && std::filesystem::is_directory(dir / "objects", ec) &&
           std::filesystem::is_directory(dir / "refs", ec);
}

/// Depth-first walk. `depth` counts levels already descended below the root.
void walk(const std::filesystem::path& dir, int depth, int maxDepth, std::vector<std::string>& out)
{
    if (depth >= maxDepth)
        return;

    std::error_code ec;
    auto it  = std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec);
    auto end = std::filesystem::directory_iterator();
    if (ec)
        return; // unreadable directory: skipped, not a scan failure

    // Manual increment (not a range-for) so an increment-time error — the
    // directory removed mid-scan, an I/O error, a network share hiccup — is
    // caught via `ec` rather than thrown: directory_iterator's implicit
    // operator++ throws on exactly that, which would cross the core/ui
    // boundary as an exception instead of an Expected. Same idiom as
    // GitRepo::watchTargets() (gitrepo.cpp).
    for (; !ec && it != end; it.increment(ec))
    {
        std::error_code dirEc;
        if (!it->is_directory(dirEc) || dirEc)
            continue;
        const std::string name = toGitPath(it->path().filename());
        if (name.empty() || name.front() == '.')
            continue;

        if (looksLikeRepo(it->path()) && GitRepo::open(it->path()))
        {
            out.push_back(toGitPath(it->path()));
            // A repository terminates the descent: its submodules and nested
            // checkouts must never be offered as repositories of their own.
            continue;
        }
        walk(it->path(), depth + 1, maxDepth, out);
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
