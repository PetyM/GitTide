#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>
#include <string>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

TEST_CASE("GitRepo::open succeeds on a real repo", "[repo]")
{
    gittide::test::TempRepo tmp;
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
}

TEST_CASE("GitRepo::open fails on a non-repo directory", "[repo]")
{
    // Create a fresh empty dir guaranteed NOT to be a git repo.
    std::mt19937_64 rng{std::random_device{}()};
    std::string name               = "gittide_test_nonrepo_" + std::to_string(rng());
    std::filesystem::path non_repo = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(non_repo);

    auto repo = gittide::GitRepo::open(non_repo);

    std::filesystem::remove_all(non_repo); // cleanup before assertions

    REQUIRE_FALSE(repo.has_value());
    REQUIRE(repo.error().code != 0);
}

TEST_CASE("status reports an untracked file as WtNew", "[repo]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("new.txt", "data");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());

    auto it = std::find_if(st->begin(),
                           st->end(),
                           [](const gittide::FileStatus& f)
                           {
                               return f.path == std::filesystem::path("new.txt");
                           });
    REQUIRE(it != st->end());
    REQUIRE(gittide::hasFlag(it->flags, gittide::StatusFlag::WtNew));
}

TEST_CASE("status reports a moved file as a single WtRenamed entry", "[repo]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("old.txt", "line one\nline two\nline three\n");
    tmp.commitAll("add old.txt");
    // Move the file: identical content under a new name, old name gone.
    tmp.writeFile("new.txt", "line one\nline two\nline three\n");
    std::filesystem::remove(tmp.path() / "old.txt");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());

    auto it = std::find_if(st->begin(),
                           st->end(),
                           [](const gittide::FileStatus& f)
                           {
                               return f.path == std::filesystem::path("new.txt");
                           });
    REQUIRE(it != st->end());
    REQUIRE(gittide::hasFlag(it->flags, gittide::StatusFlag::WtRenamed));
    REQUIRE(it->oldPath == std::filesystem::path("old.txt"));
    // The rename collapses into one row — no separate deletion of the old path.
    REQUIRE(std::none_of(st->begin(),
                         st->end(),
                         [](const gittide::FileStatus& f)
                         {
                             return f.path == std::filesystem::path("old.txt");
                         }));
}

TEST_CASE("status detects a move into another directory as a rename", "[repo]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("src/thing.txt", "one\ntwo\nthree\nfour\n");
    tmp.commitAll("add src/thing.txt");
    // Move to a different folder, same file name and content.
    tmp.writeFile("lib/thing.txt", "one\ntwo\nthree\nfour\n");
    std::filesystem::remove(tmp.path() / "src" / "thing.txt");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    auto it = std::find_if(st->begin(),
                           st->end(),
                           [](const gittide::FileStatus& f)
                           {
                               return f.path == std::filesystem::path("lib/thing.txt");
                           });
    REQUIRE(it != st->end());
    REQUIRE(gittide::hasFlag(it->flags, gittide::StatusFlag::WtRenamed));
    // The full source path is preserved, so the UI can show the folder change.
    REQUIRE(it->oldPath == std::filesystem::path("src/thing.txt"));
}

TEST_CASE("status reports a committed-then-modified file as WtModified", "[repo]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one");
    tmp.commitAll("add a.txt");
    tmp.writeFile("a.txt", "two"); // modify after commit

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    auto it = std::find_if(st->begin(),
                           st->end(),
                           [](const gittide::FileStatus& f)
                           {
                               return f.path == std::filesystem::path("a.txt");
                           });
    REQUIRE(it != st->end());
    REQUIRE(gittide::hasFlag(it->flags, gittide::StatusFlag::WtModified));
}
