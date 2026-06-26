#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

namespace {
bool hasSummary(const std::vector<CommitNode>& v, const std::string& s)
{
    return std::any_of(v.begin(), v.end(),
                       [&](const CommitNode& c) { return c.summary == s; });
}
} // namespace

TEST_CASE("logAllRefs reaches commits on every branch, log only HEAD", "[logallrefs]")
{
    test::TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "1");
    repo.commitAll("base commit");

    auto git = GitRepo::open(repo.path());
    REQUIRE(git.has_value());

    // Branch off base, switch to it, commit there.
    auto head = git->log(1);
    REQUIRE(head.has_value());
    REQUIRE_FALSE(head->empty());
    REQUIRE(git->createBranch("feature", head->front().oid).has_value());
    REQUIRE(git->checkoutBranch("feature").has_value());
    repo.writeFile("b.txt", "2");
    repo.commitAll("feature commit");

    // Back on feature: log() (HEAD) sees base + feature, not master-only extras.
    // Make master diverge so the two sets differ.
    REQUIRE(git->checkoutBranch("master").has_value());
    repo.writeFile("c.txt", "3");
    repo.commitAll("master commit");

    auto reopened = GitRepo::open(repo.path());
    REQUIRE(reopened.has_value());

    auto head_only = reopened->log(0);
    auto all       = reopened->logAllRefs(0);
    REQUIRE(head_only.has_value());
    REQUIRE(all.has_value());

    // HEAD is master: sees master + base, NOT feature.
    REQUIRE(hasSummary(*head_only, "master commit"));
    REQUIRE_FALSE(hasSummary(*head_only, "feature commit"));

    // logAllRefs sees every branch tip.
    REQUIRE(hasSummary(*all, "master commit"));
    REQUIRE(hasSummary(*all, "feature commit"));
    REQUIRE(hasSummary(*all, "base commit"));
}

TEST_CASE("logAllRefs includes tagged commits", "[logallrefs]")
{
    test::TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "1");
    repo.commitAll("tagged base");
    repo.tagHead("v1.0");

    auto git = GitRepo::open(repo.path());
    REQUIRE(git.has_value());
    auto all = git->logAllRefs(0);
    REQUIRE(all.has_value());
    REQUIRE(hasSummary(*all, "tagged base"));
}
