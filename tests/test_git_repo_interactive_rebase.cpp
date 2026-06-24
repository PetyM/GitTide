#include "gittide/gitrepo.hpp"
#include "gittide/rebase.hpp"
#include "gittide/diff.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
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

TEST_CASE("interactive rebase pauses on conflict, continue finishes after resolve", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "A\n");            // edits same line
    tmp.commitAll("A");
    tmp.writeFile("a.txt", "B\n");            // edits same line again
    tmp.commitAll("B");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(tmp, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidB},   // reorder → B before A conflicts
                     {RebaseAction::Pick, oidA} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);
    REQUIRE(out->pause == gittide::RebasePause::Conflict);

    auto st = repo->rebaseState();
    REQUIRE(st.inProgress);
    REQUIRE(st.interactive);
    REQUIRE(st.pause == gittide::RebasePause::Conflict);
    REQUIRE(st.total == 2);
    REQUIRE(st.conflictedPaths.size() == 1);

    tmp.writeFile("a.txt", "resolved\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto cont = repo->continueRebase();
    REQUIRE(cont.has_value());
    // second pick (A) also conflicts against the resolved B → resolve again
    if (cont->conflicted)
    {
        tmp.writeFile("a.txt", "resolved2\n");
        REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());
        cont = repo->continueRebase();
        REQUIRE(cont.has_value());
    }
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);
}

TEST_CASE("interactive abort restores the exact pre-rebase tip", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "A\n");
    tmp.commitAll("A");
    tmp.writeFile("a.txt", "B\n");
    tmp.commitAll("B");
    auto before = repo->head();
    REQUIRE(before.has_value());

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(tmp, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidB}, {RebaseAction::Pick, oidA} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);

    REQUIRE(repo->abortRebase().has_value());
    REQUIRE_FALSE(repo->rebaseState().inProgress);
    auto after = repo->head();
    REQUIRE(after.has_value());
    REQUIRE(after->oid == before->oid);
}

// ---------------------------------------------------------------------------
// Task 4: Reword
// ---------------------------------------------------------------------------

TEST_CASE("interactive reword of an older commit pauses for a message", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A original");
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(tmp, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Reword, oidA}, {RebaseAction::Pick, oidB} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);
    REQUIRE(out->pause == gittide::RebasePause::Message);

    auto st = repo->rebaseState();
    REQUIRE(st.pause == gittide::RebasePause::Message);
    REQUIRE(st.messagePrefill.rfind("A original", 0) == 0); // prefilled with the old message

    auto cont = repo->continueRebase("A reworded\n");
    REQUIRE(cont.has_value());
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    auto after = repo->log(10);
    REQUIRE(after->at(1).summary == "A reworded");
    REQUIRE(after->at(0).summary == "B");
}

TEST_CASE("continue without a message errors on a reword pause", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A");

    auto hist = repo->log(10);
    const std::string oidA = hist->at(0).oid;
    const std::string base = firstParentOf(tmp, oidA);
    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Reword, oidA} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->pause == gittide::RebasePause::Message);

    // Continuing with no message re-pauses (does not finish, does not error-out the repo).
    auto cont = repo->continueRebase(); // nullopt
    REQUIRE(cont.has_value());
    REQUIRE(cont->pause == gittide::RebasePause::Message);
    REQUIRE(repo->rebaseState().inProgress);
}

// ---------------------------------------------------------------------------
// Task 5: Squash
// ---------------------------------------------------------------------------

TEST_CASE("interactive squash folds a commit into the previous, pausing for message", "[rebase-i]")
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

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidA}, {RebaseAction::Squash, oidB} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->pause == gittide::RebasePause::Message);

    auto st = repo->rebaseState();
    REQUIRE(st.pause == gittide::RebasePause::Message);
    // Combined prefill carries both A's and B's messages.
    REQUIRE(st.messagePrefill.find("A") != std::string::npos);
    REQUIRE(st.messagePrefill.find("B") != std::string::npos);

    auto cont = repo->continueRebase("A and B combined\n");
    REQUIRE(cont.has_value());
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    auto after = repo->log(10);
    // newest-first: [combined, c0] — two commits, not three.
    REQUIRE(after->size() == 2);
    REQUIRE(after->at(0).summary == "A and B combined");
    // both files present in the single squashed commit.
    REQUIRE(std::filesystem::exists(tmp.path() / "a.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "b.txt"));
}

