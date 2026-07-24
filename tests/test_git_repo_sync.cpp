#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::test::TempRepo;

TEST_CASE("syncStatus reports no upstream when none is set", "[sync][status]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE_FALSE(st->hasUpstream);
}

TEST_CASE("remoteUrl returns the configured url of a remote", "[sync][remote]")
{
    TempRepo repo;
    auto     bare = repo.addBareRemote("origin");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto url = gr->remoteUrl("origin");
    REQUIRE(url);
    REQUIRE(url->find(bare.generic_string()) != std::string::npos); // file:// url to the bare repo

    auto missing = gr->remoteUrl("nope");
    REQUIRE_FALSE(missing); // unknown remote is an error, not empty
}

TEST_CASE("syncStatus reports ahead after a local-only commit", "[sync][status]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    repo.writeFile("a.txt", "two");
    repo.commitAll("c2"); // local-only

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->hasUpstream);
    REQUIRE(st->ahead == 1);
    REQUIRE(st->behind == 0);
    REQUIRE(st->remoteName == "origin");
    REQUIRE(st->upstreamName == "origin/master");
}

TEST_CASE("fetch updates remote-tracking and reports behind", "[sync][fetch]")
{
    // origin starts at c1; a *second* clone pushes c2; our repo fetches and is behind 1.
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    // Second working clone of the bare, adds c2, pushes it.
    TempRepo other;
    other.cloneFrom(bare);                 // helper added below
    other.setIdentity("Other", "o@example.com");
    other.writeFile("a.txt", "two");
    other.commitAll("c2");
    other.pushBranch("origin", "master");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto fr = gr->fetch("origin", gittide::Credentials{}, [](unsigned, unsigned) { return true; });
    REQUIRE(fr);

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->hasUpstream);
    REQUIRE(st->behind == 1);
    REQUIRE(st->ahead == 0);
}

TEST_CASE("fetch aborts when the progress callback returns false", "[sync][fetch][cancel]")
{
    // A callback returning false is the cancellation signal: the trampoline
    // returns -1 and git_remote_fetch fails. Set up a real object to transfer
    // so the transfer-progress callback actually fires.
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    TempRepo other;
    other.cloneFrom(bare);
    other.setIdentity("Other", "o@example.com");
    other.writeFile("a.txt", "two");
    other.commitAll("c2");
    other.pushBranch("origin", "master");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto fr = gr->fetch("origin", gittide::Credentials{}, [](unsigned, unsigned) { return false; });
    REQUIRE_FALSE(fr); // cancelled → error, not success
}

TEST_CASE("push aborts when the progress callback returns false", "[sync][push][cancel]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    repo.writeFile("a.txt", "two");
    repo.commitAll("c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto r = gr->push("origin", "master", /*setUpstream=*/false, gittide::Credentials{},
                      [](unsigned, unsigned) { return false; });
    REQUIRE_FALSE(r); // cancelled during upload → error
}

TEST_CASE("pullStrategy round-trips through git config", "[sync][strategy]")
{
    TempRepo repo;
    repo.writeFile("a.txt", "one");
    repo.setIdentity("Test", "test@example.com");
    repo.commitAll("c1");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);

    // Default (unset) => FastForwardOnly.
    auto s0 = gr->pullStrategy();
    REQUIRE(s0);
    REQUIRE(*s0 == gittide::PullStrategy::FastForwardOnly);

    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::Rebase));
    auto s1 = gr->pullStrategy();
    REQUIRE(s1);
    REQUIRE(*s1 == gittide::PullStrategy::Rebase);

    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::FastForwardOnly));
    auto s2 = gr->pullStrategy();
    REQUIRE(s2);
    REQUIRE(*s2 == gittide::PullStrategy::FastForwardOnly);
}

TEST_CASE("pull fast-forwards a behind branch", "[sync][pull]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    TempRepo other;
    other.cloneFrom(bare);
    other.setIdentity("Other", "o@example.com");
    other.writeFile("b.txt", "two");
    other.commitAll("c2");
    other.pushBranch("origin", "master");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::FastForwardOnly));
    REQUIRE(gr->pull(gittide::Credentials{}, [](unsigned, unsigned) { return true; }));

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->behind == 0);
    REQUIRE(st->ahead == 0);
}

TEST_CASE("pull fast-forward fails on a diverged branch", "[sync][pull]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    TempRepo other;
    other.cloneFrom(bare);
    other.setIdentity("Other", "o@example.com");
    other.writeFile("b.txt", "remote");
    other.commitAll("remote-c2");
    other.pushBranch("origin", "master");

    // Local diverges with its own c2.
    repo.writeFile("c.txt", "local");
    repo.commitAll("local-c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::FastForwardOnly));
    auto r = gr->pull(gittide::Credentials{}, [](unsigned, unsigned) { return true; });
    REQUIRE_FALSE(r); // cannot fast-forward
}

TEST_CASE("pull rebase replays local commits onto upstream", "[sync][pull]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    TempRepo other;
    other.cloneFrom(bare);
    other.setIdentity("Other", "o@example.com");
    other.writeFile("b.txt", "remote");
    other.commitAll("remote-c2");
    other.pushBranch("origin", "master");

    repo.writeFile("c.txt", "local");
    repo.commitAll("local-c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::Rebase));
    REQUIRE(gr->pull(gittide::Credentials{}, [](unsigned, unsigned) { return true; }));

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->behind == 0);
    REQUIRE(st->ahead == 1); // local-c2 replayed on top of remote-c2
}

TEST_CASE("push sends local commits to the remote and clears ahead", "[sync][push]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master"); // baseline + upstream

    repo.writeFile("a.txt", "two");
    repo.commitAll("c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->push("origin", "master", /*setUpstream=*/false, gittide::Credentials{},
                     [](unsigned, unsigned) { return true; }));

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->ahead == 0);
}

TEST_CASE("push with setUpstream publishes a branch with no upstream", "[sync][push]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin"); // remote exists, but no upstream set, nothing pushed

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);

    auto before = gr->syncStatus();
    REQUIRE(before);
    REQUIRE_FALSE(before->hasUpstream);

    REQUIRE(gr->push("origin", "master", /*setUpstream=*/true, gittide::Credentials{},
                     [](unsigned, unsigned) { return true; }));

    auto after = gr->syncStatus();
    REQUIRE(after);
    REQUIRE(after->hasUpstream);
    REQUIRE(after->ahead == 0);
    REQUIRE(after->behind == 0);
}
