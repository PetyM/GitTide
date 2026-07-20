#include <catch2/catch_test_macros.hpp>

#include <string>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

TEST_CASE("log captures the author email on each CommitNode", "[commit-email]")
{
    // TempRepo::commitAll commits with a fixed "Test <test@example.com>" author.
    test::TempRepo repo;
    repo.writeFile("a.txt", "1");
    repo.commitAll("first commit");

    auto git = GitRepo::open(repo.path());
    REQUIRE(git.has_value());

    auto head = git->log(1);
    REQUIRE(head.has_value());
    REQUIRE_FALSE(head->empty());
    CHECK(head->front().author == "Test");
    CHECK(head->front().email == "test@example.com");
}
