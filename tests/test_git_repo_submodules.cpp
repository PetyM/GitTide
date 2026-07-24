#include "gittide/gitrepo.hpp"
#include "gittide/submodule.hpp"
#include "support/temprepo.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using gittide::GitRepo;
using gittide::SubmoduleStatus;

TEST_CASE("status tags a changed submodule and splits pointer-move from dirtiness", "[submodules][status]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "x\n");
    child.commitAll("c1");
    child.writeFile("a.txt", "x2\n");
    child.commitAll("c2"); // child HEAD = c2

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path()); // pins c2, checks out c2
    parent.commitAll("add submodule");

    auto childRepo = GitRepo::open(parent.path() / "libchild");
    REQUIRE(childRepo.has_value());
    auto commits = childRepo->log();
    REQUIRE(commits.has_value());
    REQUIRE(commits->size() == 2);
    const std::string c1 = commits->back().oid; // oldest

    const auto submoduleRow = [](const std::vector<gittide::FileStatus>& st)
    {
        return std::find_if(st.begin(), st.end(),
                            [](const gittide::FileStatus& f) { return f.path == "libchild"; });
    };

    SECTION("clean pointer move: submodule HEAD != pin, working tree clean")
    {
        REQUIRE(childRepo->checkoutCommit(c1).has_value()); // HEAD = c1, clean

        auto repo = GitRepo::open(parent.path());
        REQUIRE(repo.has_value());
        auto st = repo->status();
        REQUIRE(st.has_value());
        auto it = submoduleRow(*st);
        REQUIRE(it != st->end());
        REQUIRE(gittide::hasFlag(it->flags, gittide::StatusFlag::Submodule));
        REQUIRE_FALSE(gittide::hasFlag(it->flags, gittide::StatusFlag::SubmoduleDirty));
    }

    SECTION("dirty: uncommitted work inside the submodule working tree")
    {
        std::ofstream(parent.path() / "libchild" / "a.txt", std::ios::binary) << "x2\nlocal\n";

        auto repo = GitRepo::open(parent.path());
        REQUIRE(repo.has_value());
        auto st = repo->status();
        REQUIRE(st.has_value());
        auto it = submoduleRow(*st);
        REQUIRE(it != st->end());
        REQUIRE(gittide::hasFlag(it->flags, gittide::StatusFlag::Submodule));
        REQUIRE(gittide::hasFlag(it->flags, gittide::StatusFlag::SubmoduleDirty));
    }
}

TEST_CASE("SubmoduleNode defaults are empty/clean", "[submodules]")
{
    gittide::SubmoduleNode n{};
    REQUIRE(n.name.empty());
    REQUIRE(n.shortOid.empty());
    REQUIRE(n.children.empty());
    REQUIRE(n.status == SubmoduleStatus::Clean);
}

TEST_CASE("submoduleTree reports a clean direct submodule with a 7-char pinned oid", "[submodules]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "x\n");
    child.commitAll("child");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add submodule");

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());
    auto tree = repo->submoduleTree();
    REQUIRE(tree.has_value());
    REQUIRE(tree->size() == 1);

    const auto& sub = (*tree)[0];
    REQUIRE(sub.name == "libchild");
    REQUIRE(sub.path == parent.path() / "libchild");
    REQUIRE(sub.shortOid.size() == 7);
    REQUIRE(sub.status == SubmoduleStatus::Clean);
    REQUIRE(sub.children.empty());
}

TEST_CASE("submoduleTree flags a working-tree edit as dirty", "[submodules]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "x\n");
    child.commitAll("child");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add submodule");

    // Modify the submodule's checked-out working tree.
    std::ofstream(parent.path() / "libchild" / "a.txt", std::ios::binary) << "x\nmore\n";

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());
    auto tree = repo->submoduleTree();
    REQUIRE(tree.has_value());
    REQUIRE(tree->size() == 1);
    REQUIRE((*tree)[0].status == SubmoduleStatus::Dirty);
}

TEST_CASE("submoduleTree recurses and reports an uninitialised nested submodule", "[submodules]")
{
    // grandchild -> child(has grandchild submodule) -> parent(has child submodule)
    gittide::test::TempRepo grand;
    grand.writeFile("g.txt", "g\n");
    grand.commitAll("grand");

    gittide::test::TempRepo child;
    child.writeFile("c.txt", "c\n");
    child.commitAll("child");
    child.addSubmodule("libgrand", grand.path());
    child.commitAll("child adds grand");

    gittide::test::TempRepo parent;
    parent.writeFile("p.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path()); // non-recursive: grand left bare
    parent.commitAll("parent adds child");

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());
    auto tree = repo->submoduleTree();
    REQUIRE(tree.has_value());
    REQUIRE(tree->size() == 1);

    const auto& lvl1 = (*tree)[0];
    REQUIRE(lvl1.name == "libchild");
    REQUIRE(lvl1.status == SubmoduleStatus::Clean);
    REQUIRE(lvl1.children.size() == 1);

    const auto& lvl2 = lvl1.children[0];
    REQUIRE(lvl2.name == "libgrand");
    REQUIRE(lvl2.status == SubmoduleStatus::Uninitialized);
    REQUIRE(lvl2.shortOid.empty());
    REQUIRE(lvl2.children.empty());
}

TEST_CASE("submoduleTree stops recursing at the depth cap", "[submodules]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "x\n");
    child.commitAll("child");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add submodule");

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());

    // At (or past) the cap the guard short-circuits before enumeration → empty,
    // proving a cyclic/deep graph cannot recurse without bound.
    auto capped = repo->submoduleTree(GitRepo::kMaxSubmoduleDepth);
    REQUIRE(capped.has_value());
    REQUIRE(capped->empty());

    // Sanity: the normal (depth 0) call still descends and finds the submodule.
    auto full = repo->submoduleTree();
    REQUIRE(full.has_value());
    REQUIRE(full->size() == 1);
}

TEST_CASE("updateSubmodules re-initialises all direct submodules", "[submodule]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "hello\n");
    child.commitAll("seed child");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("seed parent");
    parent.addSubmodule("sub", child.path());
    parent.commitAll("add submodule");

    auto repo = gittide::GitRepo::open(parent.path());
    REQUIRE(repo);

    // Force the submodule into a broken state via deinit: removes source files but
    // keeps the .git gitlink, so libgit2 sees it as Dirty (not Uninitialized).
    REQUIRE(repo->deinitSubmodule("sub"));
    {
        auto tree = repo->submoduleTree();
        REQUIRE(tree);
        REQUIRE(tree->size() == 1);
        REQUIRE((*tree)[0].status == gittide::SubmoduleStatus::Dirty);
    }

    // Update all direct submodules → back to Clean.
    REQUIRE(repo->updateSubmodules());

    auto tree = repo->submoduleTree();
    REQUIRE(tree);
    REQUIRE(tree->size() == 1);
    REQUIRE((*tree)[0].status == gittide::SubmoduleStatus::Clean);
}
