#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using gittide::GitRepo;

TEST_CASE("deinit empties a submodule working dir; reinit restores it", "[submodule][merge]")
{
    namespace fs = std::filesystem;
    // 1. A repo to be used as the submodule source.
    gittide::test::TempRepo sub;
    sub.setIdentity("Test", "test@example.com");
    sub.writeFile("lib.txt", "v1\n");
    sub.commitAll("sub c1");

    // 2. A superproject that adds `sub` as a submodule at "vendor/lib".
    gittide::test::TempRepo super;
    super.setIdentity("Test", "test@example.com");
    super.writeFile("top.txt", "top\n");
    super.commitAll("super c1");
    // Add the submodule via TempRepo's addSubmodule helper (uses libgit2 directly).
    super.addSubmodule("vendor/lib", sub.path());
    super.commitAll("super: add vendor/lib submodule");

    auto repo = GitRepo::open(super.path());
    REQUIRE(repo.has_value());
    REQUIRE(fs::exists(super.path() / "vendor/lib/lib.txt"));

    REQUIRE(repo->deinitSubmodule("vendor/lib").has_value());
    REQUIRE_FALSE(fs::exists(super.path() / "vendor/lib/lib.txt")); // working dir emptied

    REQUIRE(repo->reinitSubmodule("vendor/lib").has_value());
    REQUIRE(fs::exists(super.path() / "vendor/lib/lib.txt")); // restored
}
