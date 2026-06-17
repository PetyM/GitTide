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
}  // namespace

TEST_CASE("GitRepo::init creates a valid repository in an empty directory", "[git_repo][init]") {
    gitgui::LibGit2Context ctx;
    auto dir = unique_empty_dir();

    auto result = gitgui::GitRepo::init(dir);
    REQUIRE(result.has_value());

    auto opened = gitgui::GitRepo::open(dir);
    REQUIRE(opened.has_value());

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitRepo::init rejects a path that already has a .git directory", "[git_repo][init]") {
    gitgui::test::TempRepo existing;  // TempRepo owns LibGit2Context

    auto result = gitgui::GitRepo::init(existing.path());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(!result.error().message.empty());
}
