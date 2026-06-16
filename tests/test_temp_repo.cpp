#include <catch2/catch_test_macros.hpp>
#include "support/TempRepo.hpp"
#include <filesystem>

TEST_CASE("TempRepo creates a real git repo on disk", "[support]") {
    gitgui::test::TempRepo repo;
    REQUIRE(std::filesystem::exists(repo.path() / ".git"));
}

TEST_CASE("TempRepo writes a file and commits it", "[support]") {
    gitgui::test::TempRepo repo;
    repo.write_file("hello.txt", "hi");
    repo.commit_all("first commit");
    REQUIRE(std::filesystem::exists(repo.path() / "hello.txt"));
}
