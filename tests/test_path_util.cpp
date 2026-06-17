#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "gittide/pathutil.hpp"

TEST_CASE("toGitPath produces forward-slash UTF-8", "[path]")
{
    std::filesystem::path p = std::filesystem::path("a") / "b c" / "ünïcode.txt";
    std::string g           = gittide::toGitPath(p);
    REQUIRE(g.find('\\') == std::string::npos);  // no backslashes even on Windows
    REQUIRE(g.find("b c") != std::string::npos); // spaces preserved verbatim
    REQUIRE(g.find("ünïcode.txt") != std::string::npos);
}

TEST_CASE("fromGitPath round-trips toGitPath", "[path]")
{
    std::filesystem::path p = std::filesystem::path("dir") / "ünïcode.txt";
    REQUIRE(gittide::fromGitPath(gittide::toGitPath(p)) == p.lexically_normal());
}
