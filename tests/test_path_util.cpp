#include <catch2/catch_test_macros.hpp>
#include "gittide/pathutil.hpp"
#include <filesystem>

TEST_CASE("to_git_path produces forward-slash UTF-8", "[path]") {
    std::filesystem::path p = std::filesystem::path("a") / "b c" / "ünïcode.txt";
    std::string g = gittide::to_git_path(p);
    REQUIRE(g.find('\\') == std::string::npos);   // no backslashes even on Windows
    REQUIRE(g.find("b c") != std::string::npos);   // spaces preserved verbatim
    REQUIRE(g.find("ünïcode.txt") != std::string::npos);
}

TEST_CASE("from_git_path round-trips to_git_path", "[path]") {
    std::filesystem::path p = std::filesystem::path("dir") / "ünïcode.txt";
    REQUIRE(gittide::from_git_path(gittide::to_git_path(p)) ==
            p.lexically_normal());
}
