#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <git2.h>

#include "gittide/pathutil.hpp"
#include "support/temprepo.hpp"

TEST_CASE("TempRepo creates a real git repo on disk", "[support]")
{
    gittide::test::TempRepo repo;
    REQUIRE(std::filesystem::exists(repo.path() / ".git"));
}

TEST_CASE("TempRepo writes a file and commits it", "[support]")
{
    gittide::test::TempRepo repo;
    repo.writeFile("hello.txt", "hi");
    repo.commitAll("first commit");
    REQUIRE(std::filesystem::exists(repo.path() / "hello.txt"));
}

TEST_CASE("TempRepo::addSubmodule registers and clones a child repository", "[temprepo]")
{
    gittide::test::TempRepo child;
    child.writeFile("readme.md", "child\n");
    child.commitAll("child init");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "parent\n");
    parent.commitAll("parent init");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add libchild submodule");

    // .gitmodules exists and the submodule working tree was checked out.
    REQUIRE(std::filesystem::exists(parent.path() / ".gitmodules"));
    REQUIRE(std::filesystem::exists(parent.path() / "libchild" / "readme.md"));

    // libgit2 sees exactly one submodule named "libchild".
    git_repository* raw = nullptr;
    REQUIRE(git_repository_open(&raw, gittide::toGitPath(parent.path()).c_str()) == 0);
    git_submodule* sm = nullptr;
    REQUIRE(git_submodule_lookup(&sm, raw, "libchild") == 0);
    git_submodule_free(sm);
    git_repository_free(raw);
}
