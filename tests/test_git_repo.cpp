#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"
#include <filesystem>
#include <string>
#include <random>
#include <algorithm>

TEST_CASE("GitRepo::open succeeds on a real repo", "[repo]") {
    gitgui::test::TempRepo tmp;
    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
}

TEST_CASE("GitRepo::open fails on a non-repo directory", "[repo]") {
    // Create a fresh empty dir guaranteed NOT to be a git repo.
    std::mt19937_64 rng{std::random_device{}()};
    std::string name = "gitgui_test_nonrepo_" + std::to_string(rng());
    std::filesystem::path non_repo = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(non_repo);

    auto repo = gitgui::GitRepo::open(non_repo);

    std::filesystem::remove_all(non_repo);  // cleanup before assertions

    REQUIRE_FALSE(repo.has_value());
    REQUIRE(repo.error().code != 0);
}

TEST_CASE("status reports an untracked file as WtNew", "[repo]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("new.txt", "data");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());

    auto it = std::find_if(st->begin(), st->end(), [](const gitgui::FileStatus& f) {
        return f.path == std::filesystem::path("new.txt");
    });
    REQUIRE(it != st->end());
    REQUIRE(gitgui::has_flag(it->flags, gitgui::StatusFlag::WtNew));
}

TEST_CASE("status reports a committed-then-modified file as WtModified", "[repo]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "one");
    tmp.commit_all("add a.txt");
    tmp.write_file("a.txt", "two");   // modify after commit

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    auto it = std::find_if(st->begin(), st->end(), [](const gitgui::FileStatus& f) {
        return f.path == std::filesystem::path("a.txt");
    });
    REQUIRE(it != st->end());
    REQUIRE(gitgui::has_flag(it->flags, gitgui::StatusFlag::WtModified));
}
