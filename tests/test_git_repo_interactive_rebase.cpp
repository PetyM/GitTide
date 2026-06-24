#include "gittide/gitrepo.hpp"
#include "gittide/rebase.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <git2.h>

using gittide::GitRepo;
using gittide::RebaseAction;
using gittide::RebaseTodo;
using gittide::RebaseTodoEntry;

namespace {
// Parent oid of `oid` (first parent), as 40-char hex — the detach base for an
// "edit from oid" plan whose oldest entry is `oid`.
std::string firstParentOf(gittide::test::TempRepo& tmp, const std::string& oid)
{
    git_repository* raw = nullptr;
    REQUIRE(git_repository_open(&raw, tmp.path().string().c_str()) == 0);
    git_oid o; REQUIRE(git_oid_fromstr(&o, oid.c_str()) == 0);
    git_commit* c = nullptr; REQUIRE(git_commit_lookup(&c, raw, &o) == 0);
    const git_oid* p = git_commit_parent_id(c, 0);
    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), p);
    git_commit_free(c); git_repository_free(raw);
    return buf;
}
} // namespace

TEST_CASE("interactive rebase reorders two non-conflicting commits", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    tmp.writeFile("a.txt", "a\n");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.commitAll("A");                       // HEAD~1
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B");                       // HEAD

    auto hist = repo->log(10);
    REQUIRE(hist.has_value());
    // history newest-first: [B, A, c0]
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(tmp, oidA); // = c0

    RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidB},   // B first now
                     {RebaseAction::Pick, oidA} };  // then A
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);
    REQUIRE(out->pause == gittide::RebasePause::None);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    auto after = repo->log(10);
    REQUIRE(after.has_value());
    // newest-first now [A, B, c0]
    REQUIRE(after->at(0).summary == "A");
    REQUIRE(after->at(1).summary == "B");
    REQUIRE(std::filesystem::exists(tmp.path() / "a.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "b.txt"));
}

TEST_CASE("interactive rebase drops a commit", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A");
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(tmp, oidA);

    RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Drop, oidA},   // drop A
                     {RebaseAction::Pick, oidB} };  // keep B
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);

    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "a.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "b.txt"));
}

TEST_CASE("RebaseTodo and extended RebaseState have sane defaults", "[rebase-i]")
{
    gittide::RebaseTodo todo;
    REQUIRE(todo.base.empty());
    REQUIRE(todo.entries.empty());

    gittide::RebaseTodoEntry e{gittide::RebaseAction::Squash, "abc"};
    REQUIRE(e.action == gittide::RebaseAction::Squash);
    REQUIRE(e.oid == "abc");

    gittide::RebaseState st;
    REQUIRE_FALSE(st.interactive);
    REQUIRE(st.pause == gittide::RebasePause::None);
    REQUIRE(st.messagePrefill.empty());

    gittide::RebaseOutcome out;
    REQUIRE_FALSE(out.conflicted);
    REQUIRE(out.pause == gittide::RebasePause::None);
}
