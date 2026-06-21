#include "gittide/gitrepo.hpp"
#include "gittide/submodule.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using gittide::GitRepo;
using gittide::SubmoduleStatus;

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
