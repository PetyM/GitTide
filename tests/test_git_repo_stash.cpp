#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

TEST_CASE("stashCount reflects the stash stack", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    REQUIRE(repo->stashCount().value() == 0);

    tmp.writeFile("a.txt", "dirty\n");
    REQUIRE(repo->stashSave("wip").value() == true); // a stash was created
    REQUIRE(repo->stashCount().value() == 1);

    REQUIRE(repo->stashPop().has_value());
    REQUIRE(repo->stashCount().value() == 0);
}

TEST_CASE("stashList returns entries newest-first with message and oid", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stashList().value().empty());

    tmp.writeFile("a.txt", "first\n");
    REQUIRE(repo->stashSave("one").value() == true);
    tmp.writeFile("a.txt", "second\n");
    REQUIRE(repo->stashSave("two").value() == true);

    auto list = repo->stashList();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    // Newest is stash@{0}.
    REQUIRE((*list)[0].index == 0);
    REQUIRE((*list)[1].index == 1);
    REQUIRE((*list)[0].message.find("two") != std::string::npos);
    REQUIRE((*list)[1].message.find("one") != std::string::npos);
    REQUIRE((*list)[0].oid.size() == 40);
}

TEST_CASE("stashApplyAt applies a chosen entry and keeps it", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "older\n");
    REQUIRE(repo->stashSave("older").value());
    tmp.writeFile("a.txt", "newer\n");
    REQUIRE(repo->stashSave("newer").value());

    // Apply the OLDER entry (index 1); it stays on the stack.
    REQUIRE(repo->stashApplyAt(1).has_value());
    REQUIRE(tmp.readFile("a.txt") == "older\n");
    REQUIRE(repo->stashCount().value() == 2);
}

TEST_CASE("stashPopAt applies and drops a chosen entry", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "x\n");
    REQUIRE(repo->stashSave("x").value());
    REQUIRE(repo->stashPopAt(0).has_value());
    REQUIRE(tmp.readFile("a.txt") == "x\n");
    REQUIRE(repo->stashCount().value() == 0);
}

TEST_CASE("stashDrop removes an entry without applying it", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "drop-me\n");
    REQUIRE(repo->stashSave("drop").value());
    REQUIRE(repo->stashDrop(0).has_value());
    REQUIRE(repo->stashCount().value() == 0);
    REQUIRE(tmp.readFile("a.txt") == "orig\n"); // not applied
}

TEST_CASE("stashClear empties the whole stack", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "1\n"); REQUIRE(repo->stashSave("1").value());
    tmp.writeFile("a.txt", "2\n"); REQUIRE(repo->stashSave("2").value());
    REQUIRE(repo->stashClear().has_value());
    REQUIRE(repo->stashCount().value() == 0);
}

TEST_CASE("stashPopAt conflict preserves the stash", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "stashed\n");
    REQUIRE(repo->stashSave("s").value());
    // Recreate a conflicting working change before popping.
    tmp.writeFile("a.txt", "conflicting\n");

    auto r = repo->stashPopAt(0);
    REQUIRE_FALSE(r.has_value());            // reported
    REQUIRE(repo->stashCount().value() == 1); // and preserved
}

TEST_CASE("stashFiles lists both tracked changes and untracked files", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("tracked.txt", "base\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // A tracked modification AND a brand-new untracked file, then stash both.
    tmp.writeFile("tracked.txt", "base\nmore\n");
    tmp.writeFile("untracked.txt", "brand new\n");
    REQUIRE(repo->stashSave("wip").value());

    auto list = repo->stashList();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 1);
    const std::string oid = (*list)[0].oid;

    auto files = repo->stashFiles(oid);
    REQUIRE(files.has_value());

    bool hasTracked = false, hasUntracked = false;
    for (const auto& f : *files)
    {
        const auto p = f.path.generic_string();
        if (p == "tracked.txt")   hasTracked = true;
        if (p == "untracked.txt") hasUntracked = true;
    }
    REQUIRE(hasTracked);
    REQUIRE(hasUntracked); // the bug: untracked file was missing from the preview

    // And its diff is retrievable (the untracked file shows as added content).
    auto d = repo->stashDiff(oid, std::filesystem::path("untracked.txt"));
    REQUIRE(d.has_value());
    REQUIRE_FALSE(d->hunks.empty());
}
