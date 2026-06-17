#include <catch2/catch_test_macros.hpp>
#include "gittide/Graph.hpp"
#include "gittide/GraphBuilder.hpp"

// Helpers
static gittide::CommitNode make_commit(std::string oid,
                                      std::vector<std::string> parents) {
    gittide::CommitNode n;
    n.oid     = std::move(oid);
    n.summary = n.oid;
    n.parents = std::move(parents);
    return n;
}

TEST_CASE("GraphBuilder: empty input returns empty layout", "[graph]") {
    auto layout = gittide::GraphBuilder::build({});
    REQUIRE(layout.rows.empty());
    REQUIRE(layout.laneCount == 0);
}

TEST_CASE("GraphBuilder: single commit (initial) at lane 0", "[graph]") {
    auto layout = gittide::GraphBuilder::build({ make_commit("a", {}) });

    REQUIRE(layout.rows.size() == 1);
    REQUIRE(layout.rows[0].commit.lane == 0);
    REQUIRE(!layout.rows[0].lineFromAbove);  // no predecessor
    REQUIRE(layout.rows[0].passThroughs.empty());
    REQUIRE(layout.rows[0].outEdges.empty());
    REQUIRE(layout.laneCount == 1);
}

TEST_CASE("GraphBuilder: linear chain — all commits at lane 0", "[graph]") {
    std::vector<gittide::CommitNode> commits = {
        make_commit("c", {"b"}),
        make_commit("b", {"a"}),
        make_commit("a", {}),
    };
    auto layout = gittide::GraphBuilder::build(commits);

    REQUIRE(layout.rows.size() == 3);
    for (const auto& row : layout.rows)
        REQUIRE(row.commit.lane == 0);
    REQUIRE(layout.laneCount == 1);
}

TEST_CASE("GraphBuilder: linear chain — outEdges and lineFromAbove", "[graph]") {
    std::vector<gittide::CommitNode> commits = {
        make_commit("b", {"a"}),
        make_commit("a", {}),
    };
    auto layout = gittide::GraphBuilder::build(commits);

    // b: HEAD, no incoming line
    REQUIRE(!layout.rows[0].lineFromAbove);
    REQUIRE(layout.rows[0].outEdges.size() == 1);
    REQUIRE(layout.rows[0].outEdges[0] == (gittide::GraphEdge{0, 0}));
    REQUIRE(layout.rows[0].passThroughs.empty());

    // a: line arrives from above
    REQUIRE(layout.rows[1].lineFromAbove);
    REQUIRE(layout.rows[1].outEdges.empty());
    REQUIRE(layout.rows[1].passThroughs.empty());
}

TEST_CASE("GraphBuilder: diamond topology (fork then merge)", "[graph]") {
    // Topological order (newest first): A(merge)→{B,C}, B→D, C→D, D(initial)
    std::vector<gittide::CommitNode> commits = {
        make_commit("a", {"b", "c"}),  // merge commit
        make_commit("b", {"d"}),
        make_commit("c", {"d"}),
        make_commit("d", {}),
    };
    auto layout = gittide::GraphBuilder::build(commits);
    REQUIRE(layout.rows.size() == 4);

    // A: merge commit at lane 0, no predecessor, two outEdges
    // passThroughs are captured BEFORE parents are assigned, so no lanes existed
    // above row A — passthrough list is empty.
    const auto& rowA = layout.rows[0];
    REQUIRE(rowA.commit.lane == 0);
    REQUIRE(!rowA.lineFromAbove);
    REQUIRE(rowA.outEdges.size() == 2);
    REQUIRE(rowA.outEdges[0] == (gittide::GraphEdge{0, 0}));  // A→B stays lane 0
    REQUIRE(rowA.outEdges[1] == (gittide::GraphEdge{0, 1}));  // A→C goes to lane 1
    REQUIRE(rowA.passThroughs.empty());  // no lanes existed above A

    // B: lane 0, line from above, passthrough lane 1 (C waiting)
    const auto& rowB = layout.rows[1];
    REQUIRE(rowB.commit.lane == 0);
    REQUIRE(rowB.lineFromAbove);
    REQUIRE(rowB.passThroughs == std::vector<int>{1});
    REQUIRE(rowB.outEdges.size() == 1);
    REQUIRE(rowB.outEdges[0] == (gittide::GraphEdge{0, 0}));  // B→D, D at lane 0

    // C: lane 1, line from above, passthrough lane 0 (D waiting)
    const auto& rowC = layout.rows[2];
    REQUIRE(rowC.commit.lane == 1);
    REQUIRE(rowC.lineFromAbove);
    REQUIRE(rowC.passThroughs == std::vector<int>{0});
    REQUIRE(rowC.outEdges.size() == 1);
    REQUIRE(rowC.outEdges[0] == (gittide::GraphEdge{1, 0}));  // C→D at lane 0

    // D: lane 0, line from above (two branches converge here), no outEdges.
    // C's slot (lane 1) is freed when C's parent D is detected as already tracked
    // at lane 0 — no ghost duplicate, so passThroughs is empty.
    const auto& rowD = layout.rows[3];
    REQUIRE(rowD.commit.lane == 0);
    REQUIRE(rowD.lineFromAbove);
    REQUIRE(rowD.passThroughs.empty());  // lane 1 freed at C, no ghost
    REQUIRE(rowD.outEdges.empty());

    REQUIRE(layout.laneCount == 2);
}

TEST_CASE("GraphBuilder: two independent branches, no merge", "[graph]") {
    // A and B are both branch heads (independent); C is base of B; D is base of A.
    // Walk order: A, B, C, D  (A and B are unrelated)
    std::vector<gittide::CommitNode> commits = {
        make_commit("a", {"d"}),
        make_commit("b", {"c"}),
        make_commit("c", {}),
        make_commit("d", {}),
    };
    auto layout = gittide::GraphBuilder::build(commits);
    REQUIRE(layout.rows.size() == 4);

    // A: lane 0, no predecessor, outEdge to D (also lane 0)
    REQUIRE(layout.rows[0].commit.lane == 0);
    REQUIRE(!layout.rows[0].lineFromAbove);

    // B: lane 1 (new branch), no predecessor
    REQUIRE(layout.rows[1].commit.lane == 1);
    REQUIRE(!layout.rows[1].lineFromAbove);

    // C: lane 1 (B's parent), line from above
    REQUIRE(layout.rows[2].commit.lane == 1);
    REQUIRE(layout.rows[2].lineFromAbove);

    // D: lane 0 (A's parent), line from above
    REQUIRE(layout.rows[3].commit.lane == 0);
    REQUIRE(layout.rows[3].lineFromAbove);

    REQUIRE(layout.laneCount == 2);
}
