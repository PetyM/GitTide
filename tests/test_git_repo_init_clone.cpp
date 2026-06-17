#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "gitgui/LibGit2Context.hpp"
#include "support/TempRepo.hpp"
#include <filesystem>
#include <random>

namespace {
std::filesystem::path unique_empty_dir() {
    std::mt19937_64 rng{std::random_device{}()};
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui_init_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};
}  // namespace

TEST_CASE("GitRepo::init creates a valid repository in an empty directory", "[git_repo][init]") {
    gitgui::LibGit2Context ctx;
    TempDir dir_guard{unique_empty_dir()};
    auto& dir = dir_guard.path;

    auto result = gitgui::GitRepo::init(dir);
    REQUIRE(result.has_value());

    auto opened = gitgui::GitRepo::open(dir);
    REQUIRE(opened.has_value());
}

TEST_CASE("GitRepo::init rejects a path that is already a git repository", "[git_repo][init]") {
    gitgui::test::TempRepo existing;  // TempRepo owns LibGit2Context

    auto result = gitgui::GitRepo::init(existing.path());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(!result.error().message.empty());
}

TEST_CASE("GitRepo::clone from file:// produces a working repo and invokes callback",
          "[git_repo][clone]") {
    gitgui::test::TempRepo source;
    source.set_identity("Test", "t@t.test");
    source.write_file("README.md", "hello\n");
    source.commit_all("initial");

    TempDir dest_guard{unique_empty_dir()};
    auto& dest = dest_guard.path;
    std::filesystem::remove_all(dest);  // clone creates dest itself

    int progress_calls = 0;
    gitgui::ProgressCallback cb = [&](unsigned, unsigned) {
        ++progress_calls;
        return true;  // continue
    };
    auto result = gitgui::GitRepo::clone(
        "file://" + source.path().generic_string(), dest, std::move(cb));

    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(dest / "README.md"));
    REQUIRE(progress_calls > 0);
}

TEST_CASE("GitRepo::clone aborts when callback returns false", "[git_repo][clone]") {
    gitgui::test::TempRepo source;
    source.set_identity("Test", "t@t.test");
    source.write_file("a.txt", "data\n");
    source.commit_all("initial");

    TempDir dest_guard{unique_empty_dir()};
    auto& dest = dest_guard.path;
    std::filesystem::remove_all(dest);

    gitgui::ProgressCallback cb = [](unsigned, unsigned) { return false; };  // cancel
    auto result = gitgui::GitRepo::clone(
        "file://" + source.path().generic_string(), dest, std::move(cb));

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("GitRepo::clone into a missing URL returns an error", "[git_repo][clone]") {
    gitgui::LibGit2Context ctx;
    TempDir dest_guard{unique_empty_dir()};
    auto& dest = dest_guard.path;
    std::filesystem::remove_all(dest);

    gitgui::ProgressCallback cb = [](unsigned, unsigned) { return true; };
    auto result = gitgui::GitRepo::clone("/no/such/gitgui-clone-src", dest, std::move(cb));

    REQUIRE_FALSE(result.has_value());
}
