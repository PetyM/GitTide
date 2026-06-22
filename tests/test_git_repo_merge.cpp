#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <fstream>
#include <iterator>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
// Build two branches that touch the same line so a merge must conflict.
// Returns an opened repo positioned on "master" with "feature" diverged.
gittide::Expected<GitRepo> conflictingRepo(gittide::test::TempRepo& tmp)
{
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("base");
    auto repo = GitRepo::open(tmp.path());
    if (!repo)
        return std::unexpected(repo.error());
    if (auto r = repo->createBranch("feature", ""); !r) return std::unexpected(r.error());
    if (auto r = repo->checkoutBranch("feature"); !r) return std::unexpected(r.error());
    tmp.writeFile("a.txt", "feature\n");
    tmp.commitAll("feature edit");
    if (auto r = repo->checkoutBranch("master"); !r) return std::unexpected(r.error());
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("main edit");
    return repo;
}
} // namespace

TEST_CASE("Conflicted flag composes and is distinct", "[merge]")
{
    StatusFlag f = StatusFlag::WtModified | StatusFlag::Conflicted;
    REQUIRE(hasFlag(f, StatusFlag::Conflicted));
    REQUIRE(hasFlag(f, StatusFlag::WtModified));
    REQUIRE_FALSE(hasFlag(StatusFlag::WtModified, StatusFlag::Conflicted));
}

TEST_CASE("mergeState reports not-in-progress for a clean repo", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE_FALSE(ms->inProgress);
    REQUIRE(ms->conflictedPaths.empty());
}

TEST_CASE("mergeBranch fast-forwards when HEAD is an ancestor", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "one\ntwo\n");
    tmp.commitAll("c2 on feature");
    REQUIRE(repo->checkoutBranch("master").has_value());

    auto out = repo->mergeBranch("feature");
    REQUIRE(out.has_value());
    REQUIRE(out->analysis == gittide::MergeAnalysis::FastForward);
    REQUIRE_FALSE(out->conflicted);
    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE_FALSE(ms->inProgress);
    // HEAD now sees the feature content.
    auto d = repo->diff(gittide::DiffTarget::WorktreeVsHead, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.empty()); // worktree matches the fast-forwarded HEAD
}

TEST_CASE("mergeBranch performs a clean merge", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.writeFile("b.txt", "separate\n");
    tmp.commitAll("base");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "feature\n");
    tmp.commitAll("feature edit");
    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("b.txt", "master\n");
    tmp.commitAll("master edit");

    auto out = repo->mergeBranch("feature");
    REQUIRE(out.has_value());
    REQUIRE(out->analysis == gittide::MergeAnalysis::Normal);
    REQUIRE_FALSE(out->conflicted);

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE(ms->inProgress);
    REQUIRE(ms->conflictedPaths.empty());
}

TEST_CASE("mergeBranch leaves conflict markers + entries on a real conflict", "[merge]")
{
    gittide::test::TempRepo tmp;
    auto repo = conflictingRepo(tmp);
    REQUIRE(repo.has_value());

    auto out = repo->mergeBranch("feature");
    REQUIRE(out.has_value());
    REQUIRE(out->analysis == gittide::MergeAnalysis::Normal);
    REQUIRE(out->conflicted);

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE(ms->inProgress);
    REQUIRE(ms->conflictedPaths.size() == 1);
    REQUIRE(ms->conflictedPaths[0].generic_string() == "a.txt");

    // The worktree file carries conflict markers.
    std::ifstream in(tmp.path() / "a.txt");
    std::string body((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(body.find("<<<<<<<") != std::string::npos);
    REQUIRE(body.find(">>>>>>>") != std::string::npos);
}

TEST_CASE("commitMerge creates a 2-parent commit and clears merge state", "[merge]")
{
    gittide::test::TempRepo tmp;
    auto repo = conflictingRepo(tmp);
    REQUIRE(repo.has_value());
    REQUIRE(repo->mergeBranch("feature")->conflicted);

    // Resolve: write a merged file and stage it (clears the conflict entry).
    tmp.writeFile("a.txt", "resolved\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto oid = repo->commitMerge(gittide::CommitRequest{"Merge branch 'feature' into master"});
    REQUIRE(oid.has_value());

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE_FALSE(ms->inProgress);          // state cleared

    auto hist = repo->log();
    REQUIRE(hist.has_value());
    // The newest commit is the merge commit (its first line matches the message).
    REQUIRE(hist->front().summary == "Merge branch 'feature' into master");
}

TEST_CASE("commitMerge refuses while conflicts remain", "[merge]")
{
    gittide::test::TempRepo tmp;
    auto repo = conflictingRepo(tmp);
    REQUIRE(repo.has_value());
    REQUIRE(repo->mergeBranch("feature")->conflicted);

    auto oid = repo->commitMerge(gittide::CommitRequest{"premature"});
    REQUIRE_FALSE(oid.has_value());          // still conflicted → error
}
