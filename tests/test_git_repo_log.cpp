#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>

#include "gittide/gitrepo.hpp"
#include "gittide/graph.hpp"
#include "gittide/libgit2context.hpp"
#include "support/temprepo.hpp"

namespace {
std::filesystem::path unique_empty_dir_log()
{
    std::mt19937_64 rng{std::random_device{}()};
    auto dir = std::filesystem::temp_directory_path() / ("gittide_log_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}
} // namespace

TEST_CASE("GitRepo::log on empty repo returns empty vector", "[git_repo][log]")
{
    gittide::LibGit2Context ctx;
    auto dir  = unique_empty_dir_log();
    auto repo = gittide::GitRepo::init(dir);
    REQUIRE(repo.has_value());

    auto result = repo->log();
    REQUIRE(result.has_value());
    REQUIRE(result->empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitRepo::log returns commits newest-first", "[git_repo][log]")
{
    gittide::test::TempRepo tmp;
    tmp.set_identity("Test", "t@t.test");
    tmp.write_file("a.txt", "a");
    tmp.commit_all("first");
    tmp.write_file("b.txt", "b");
    tmp.commit_all("second");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto result = repo->log();
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    REQUIRE(result->at(0).summary == "second");
    REQUIRE(result->at(1).summary == "first");
    REQUIRE(!result->at(0).oid.empty());
    REQUIRE(result->at(0).oid.size() == 40); // full SHA-1 hex
    REQUIRE(result->at(0).parents.size() == 1);
    REQUIRE(result->at(0).parents[0] == result->at(1).oid);
    REQUIRE(result->at(1).parents.empty());
    REQUIRE(!result->at(0).author.empty());
    REQUIRE(result->at(0).time > 0);
}

TEST_CASE("GitRepo::log respects the limit parameter", "[git_repo][log]")
{
    gittide::test::TempRepo tmp;
    tmp.set_identity("Test", "t@t.test");
    for (int i = 0; i < 5; ++i)
    {
        tmp.write_file("f.txt", std::to_string(i));
        tmp.commit_all("commit " + std::to_string(i));
    }

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto result = repo->log(3);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);
}
