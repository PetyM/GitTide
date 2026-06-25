#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
bool anyStaged(const std::vector<gittide::FileStatus>& v)
{
    return std::any_of(v.begin(), v.end(), [](const auto& f) {
        return hasFlag(f.flags, StatusFlag::IndexNew)
            || hasFlag(f.flags, StatusFlag::IndexModified)
            || hasFlag(f.flags, StatusFlag::IndexDeleted);
    });
}
} // namespace

TEST_CASE("undoLastCommit moves HEAD to the parent and keeps the change staged", "[undo]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("first");
    tmp.writeFile("b.txt", "two\n");
    tmp.commitAll("second");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto hist = repo->log();
    REQUIRE(hist.has_value());
    REQUIRE(hist->size() == 2);
    const std::string firstOid = hist->back().oid; // oldest is last (newest-first log)

    REQUIRE(repo->undoLastCommit().has_value());

    // HEAD now points at the parent ("first"); the "second" commit is gone.
    auto after = repo->log();
    REQUIRE(after.has_value());
    REQUIRE(after->size() == 1);
    REQUIRE(after->front().oid == firstOid);

    // The undone commit's content (b.txt) remains staged (soft reset).
    auto st = repo->status();
    REQUIRE(st.has_value());
    REQUIRE(anyStaged(*st));
    REQUIRE(std::any_of(st->begin(), st->end(), [](const auto& f) {
        return f.path == "b.txt" && hasFlag(f.flags, StatusFlag::IndexNew);
    }));
}

TEST_CASE("undoLastCommit errors on a root commit (no parent)", "[undo]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("root");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE_FALSE(repo->undoLastCommit().has_value());
}

TEST_CASE("undoLastCommit errors on an unborn branch", "[undo]")
{
    gittide::test::TempRepo tmp; // fresh init, no commit => unborn HEAD
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE_FALSE(repo->undoLastCommit().has_value());
}

TEST_CASE("undoLastCommit errors on a detached HEAD", "[undo]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("first");
    tmp.writeFile("b.txt", "two\n");
    tmp.commitAll("second");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto hist = repo->log();
    REQUIRE(hist.has_value());
    const std::string firstOid = hist->back().oid;
    REQUIRE(repo->checkoutCommit(firstOid).has_value()); // detach

    REQUIRE_FALSE(repo->undoLastCommit().has_value());
}
