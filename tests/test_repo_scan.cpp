#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <git2.h>
#include <random>
#include <string>

#include "gittide/libgit2context.hpp"
#include "gittide/pathutil.hpp"
#include "gittide/reposcan.hpp"
#include "support/temprepo.hpp"

namespace {

// A unique empty directory under the system temp dir, removed on destruction.
class TempDir
{
public:
    TempDir()
        : m_dir(std::filesystem::temp_directory_path() / ("gittide-scan-" + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_dir);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const
    {
        return m_dir;
    }

private:
    std::filesystem::path m_dir;
};

// git-init `p` (creating parents), leaving an empty repository behind.
void initRepoAt(const std::filesystem::path& p, bool bare = false)
{
    std::filesystem::create_directories(p);
    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, gittide::toGitPath(p).c_str(), bare ? 1 : 0) == 0);
    git_repository_free(raw);
}

bool contains(const std::vector<std::string>& v, const std::filesystem::path& p)
{
    return std::find(v.begin(), v.end(), gittide::toGitPath(p)) != v.end();
}

} // namespace

TEST_CASE("scanForRepos finds repositories in direct children", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;
    initRepoAt(root.path() / "api");
    initRepoAt(root.path() / "web");
    std::filesystem::create_directories(root.path() / "notes"); // plain folder

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 1});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 2);
    REQUIRE(contains(*found, root.path() / "api"));
    REQUIRE(contains(*found, root.path() / "web"));
}

TEST_CASE("scanForRepos honours maxDepth", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;
    initRepoAt(root.path() / "acme" / "api");

    auto shallow = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 1});
    REQUIRE(shallow.has_value());
    REQUIRE(shallow->empty());

    auto deep = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 2});
    REQUIRE(deep.has_value());
    REQUIRE(deep->size() == 1);
    REQUIRE(contains(*deep, root.path() / "acme" / "api"));
}

TEST_CASE("scanForRepos stops descending at a repository", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;
    initRepoAt(root.path() / "api");
    initRepoAt(root.path() / "api" / "vendor" / "lib"); // nested checkout

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 4});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, root.path() / "api"));
}

TEST_CASE("scanForRepos never returns a repository's submodules", "[scan]")
{
    // Submodules reach the user through the parent repo's submodule tree.
    // Returning them here would duplicate every submodule as a top-level repo.
    gittide::test::TempRepo parent;
    gittide::test::TempRepo child;
    parent.setIdentity("Test", "test@example.com");
    parent.writeFile("a.txt", "one");
    parent.commitAll("c1");
    child.setIdentity("Test", "test@example.com");
    child.writeFile("b.txt", "two");
    child.commitAll("c1");
    parent.addSubmodule("vendor/lib", child.path());

    auto found = gittide::scanForRepos(parent.path(), gittide::ScanOptions{.maxDepth = 4});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, parent.path())); // the parent only — not vendor/lib
}

TEST_CASE("scanForRepos skips dot-directories", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;
    initRepoAt(root.path() / ".cache" / "api");

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 3});

    REQUIRE(found.has_value());
    REQUIRE(found->empty());
}

TEST_CASE("scanForRepos finds a bare repository", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;
    initRepoAt(root.path() / "mirror.git", /*bare=*/true);

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 1});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, root.path() / "mirror.git"));
}

TEST_CASE("scanForRepos returns the root itself when it is a repository", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;
    initRepoAt(root.path() / "api");

    auto found = gittide::scanForRepos(root.path() / "api", gittide::ScanOptions{.maxDepth = 2});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, root.path() / "api"));
}

TEST_CASE("scanForRepos returns an empty list for a tree with no repositories", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;
    std::filesystem::create_directories(root.path() / "docs" / "images");

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 3});

    REQUIRE(found.has_value());
    REQUIRE(found->empty());
}

TEST_CASE("scanForRepos errors when the root is missing", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir root;

    auto found = gittide::scanForRepos(root.path() / "nope");

    REQUIRE_FALSE(found.has_value());
}
