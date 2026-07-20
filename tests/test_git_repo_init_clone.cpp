#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>
#include <string>

#include "gittide/gitrepo.hpp"
#include "gittide/libgit2context.hpp"
#include "support/temprepo.hpp"

namespace {
std::filesystem::path unique_empty_dir()
{
    std::mt19937_64 rng{std::random_device{}()};
    auto dir = std::filesystem::temp_directory_path() / ("gittide_init_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

// Build a RFC 8089 file:// URL from a local path. POSIX paths are absolute and
// start with '/', so "file://" + "/tmp/x" already yields the required three
// slashes; Windows paths start with a drive letter ("C:/x"), which needs an
// explicit leading slash ("file:///C:/x") or libgit2 treats "C:" as the host.
std::string fileUrl(const std::filesystem::path& p)
{
    std::string generic = p.generic_string();
    return generic.starts_with('/') ? "file://" + generic : "file:///" + generic;
}

struct TempDir
{
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p)
        : path(std::move(p))
    {
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;
};
} // namespace

TEST_CASE("GitRepo::init creates a valid repository in an empty directory", "[git_repo][init]")
{
    gittide::LibGit2Context ctx;
    TempDir dir_guard{unique_empty_dir()};
    auto& dir = dir_guard.path;

    auto result = gittide::GitRepo::init(dir);
    REQUIRE(result.has_value());

    auto opened = gittide::GitRepo::open(dir);
    REQUIRE(opened.has_value());
}

TEST_CASE("GitRepo::init rejects a path that is already a git repository", "[git_repo][init]")
{
    gittide::test::TempRepo existing; // TempRepo owns LibGit2Context

    auto result = gittide::GitRepo::init(existing.path());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(!result.error().message.empty());
}

TEST_CASE("GitRepo::clone from file:// produces a working repo and invokes callback", "[git_repo][clone]")
{
    gittide::test::TempRepo source;
    source.setIdentity("Test", "t@t.test");
    source.writeFile("README.md", "hello\n");
    source.commitAll("initial");

    TempDir dest_guard{unique_empty_dir()};
    auto& dest = dest_guard.path;
    std::filesystem::remove_all(dest); // clone creates dest itself

    int progress_calls           = 0;
    gittide::ProgressCallback cb = [&](unsigned, unsigned)
    {
        ++progress_calls;
        return true; // continue
    };
    auto result = gittide::GitRepo::clone(fileUrl(source.path()), dest, gittide::Credentials{}, std::move(cb));

    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(dest / "README.md"));
    REQUIRE(progress_calls > 0);
}

TEST_CASE("GitRepo::clone aborts when callback returns false", "[git_repo][clone]")
{
    gittide::test::TempRepo source;
    source.setIdentity("Test", "t@t.test");
    source.writeFile("a.txt", "data\n");
    source.commitAll("initial");

    TempDir dest_guard{unique_empty_dir()};
    auto& dest = dest_guard.path;
    std::filesystem::remove_all(dest);

    gittide::ProgressCallback cb = [](unsigned, unsigned)
    {
        return false;
    }; // cancel
    auto result = gittide::GitRepo::clone(fileUrl(source.path()), dest, gittide::Credentials{}, std::move(cb));

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("GitRepo::clone into a missing URL returns an error", "[git_repo][clone]")
{
    gittide::LibGit2Context ctx;
    TempDir dest_guard{unique_empty_dir()};
    auto& dest = dest_guard.path;
    std::filesystem::remove_all(dest);

    gittide::ProgressCallback cb = [](unsigned, unsigned)
    {
        return true;
    };
    auto result = gittide::GitRepo::clone("/no/such/gittide-clone-src", dest, gittide::Credentials{}, std::move(cb));

    REQUIRE_FALSE(result.has_value());
}
