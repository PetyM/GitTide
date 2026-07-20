#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

TEST_CASE("aheadBehind counts commits between two oids", "[aheadbehind]")
{
    test::TempRepo repo;
    repo.setIdentity("T", "t@e.x");
    repo.writeFile("a.txt", "1");
    repo.commitAll("c1");

    auto g = GitRepo::open(repo.path());
    REQUIRE(g.has_value());
    const std::string o1 = g->head().value().oid;

    repo.writeFile("a.txt", "2");
    repo.commitAll("c2");
    const std::string o2 = g->head().value().oid;

    repo.writeFile("a.txt", "3");
    repo.commitAll("c3");
    const std::string o3 = g->head().value().oid;

    auto ahead = g->aheadBehind(o3, o1); // o3 is 2 past o1
    REQUIRE(ahead.has_value());
    REQUIRE(ahead->first == 2);
    REQUIRE(ahead->second == 0);

    auto behind = g->aheadBehind(o1, o3); // o1 is 2 behind o3
    REQUIRE(behind.has_value());
    REQUIRE(behind->first == 0);
    REQUIRE(behind->second == 2);

    auto equal = g->aheadBehind(o2, o2);
    REQUIRE(equal.has_value());
    REQUIRE(equal->first == 0);
    REQUIRE(equal->second == 0);
}

TEST_CASE("submoduleTree reports submodule branch/dirty/ahead-behind vs pin", "[submodule-detail]")
{
    // Child repo: c1, c2 on master; capture c1's oid.
    test::TempRepo child;
    child.setIdentity("T", "t@e.x");
    child.writeFile("f.txt", "1");
    child.commitAll("c1");
    auto cg = GitRepo::open(child.path());
    REQUIRE(cg.has_value());
    const std::string oid1 = cg->head().value().oid;
    child.writeFile("f.txt", "2");
    child.commitAll("c2"); // child master tip = c2

    // Parent pins the submodule at c2 (child's checked-out HEAD when cloned).
    test::TempRepo parent;
    parent.setIdentity("T", "t@e.x");
    parent.writeFile("top.txt", "p");
    parent.commitAll("base");
    parent.addSubmodule("libc", child.path());
    parent.commitAll("add submodule"); // pin = c2

    auto pg = GitRepo::open(parent.path());
    REQUIRE(pg.has_value());

    auto tree0 = pg->submoduleTree();
    REQUIRE(tree0.has_value());
    REQUIRE(tree0->size() == 1);
    const SubmoduleNode& s0 = (*tree0)[0];
    REQUIRE(s0.ahead == 0);            // freshly added: on the pin
    REQUIRE(s0.behind == 0);
    REQUIRE(s0.dirtyCount == 0);       // clean working tree
    REQUIRE(s0.headShortOid.size() == 7);
    REQUIRE((s0.detached || !s0.branch.empty())); // on a branch or detached, either is valid

    // Move the submodule checkout back to c1 → 1 behind the pin (c2).
    const std::filesystem::path subPath = s0.path;
    auto sg = GitRepo::open(subPath);
    REQUIRE(sg.has_value());
    REQUIRE(sg->checkoutCommit(oid1).has_value());

    auto tree1 = pg->submoduleTree();
    REQUIRE(tree1.has_value());
    const SubmoduleNode& s1 = (*tree1)[0];
    REQUIRE(s1.detached == true);
    REQUIRE(s1.ahead == 0);
    REQUIRE(s1.behind == 1);
    REQUIRE(s1.headShortOid == oid1.substr(0, 7));

    // Dirty the submodule working tree → dirtyCount >= 1.
    {
        std::ofstream(subPath / "f.txt") << "dirty\n";
    }
    auto tree2 = pg->submoduleTree();
    REQUIRE(tree2.has_value());
    REQUIRE((*tree2)[0].dirtyCount >= 1);
}
