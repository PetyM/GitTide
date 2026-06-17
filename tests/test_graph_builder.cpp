#include <catch2/catch_test_macros.hpp>
#include "gitgui/Graph.hpp"
#include "gitgui/GraphBuilder.hpp"

// Helpers
static gitgui::CommitNode make_commit(std::string oid,
                                      std::vector<std::string> parents) {
    gitgui::CommitNode n;
    n.oid     = std::move(oid);
    n.summary = n.oid;
    n.parents = std::move(parents);
    return n;
}

TEST_CASE("GraphBuilder: empty input returns empty layout", "[graph]") {
    auto layout = gitgui::GraphBuilder::build({});
    REQUIRE(layout.rows.empty());
    REQUIRE(layout.laneCount == 0);
}

TEST_CASE("GraphBuilder: single commit (initial) at lane 0", "[graph]") {
    auto layout = gitgui::GraphBuilder::build({ make_commit("a", {}) });

    REQUIRE(layout.rows.size() == 1);
    REQUIRE(layout.rows[0].commit.lane == 0);
    REQUIRE(!layout.rows[0].lineFromAbove);  // no predecessor
    REQUIRE(layout.rows[0].passThroughs.empty());
    REQUIRE(layout.rows[0].outEdges.empty());
    REQUIRE(layout.laneCount == 1);
}

TEST_CASE("GraphBuilder: linear chain — all commits at lane 0", "[graph]") {
    std::vector<gitgui::CommitNode> commits = {
        make_commit("c", {"b"}),
        make_commit("b", {"a"}),
        make_commit("a", {}),
    };
    auto layout = gitgui::GraphBuilder::build(commits);

    REQUIRE(layout.rows.size() == 3);
    for (const auto& row : layout.rows)
        REQUIRE(row.commit.lane == 0);
    REQUIRE(layout.laneCount == 1);
}

TEST_CASE("GraphBuilder: linear chain — outEdges and lineFromAbove", "[graph]") {
    std::vector<gitgui::CommitNode> commits = {
        make_commit("b", {"a"}),
        make_commit("a", {}),
    };
    auto layout = gitgui::GraphBuilder::build(commits);

    // b: HEAD, no incoming line
    REQUIRE(!layout.rows[0].lineFromAbove);
    REQUIRE(layout.rows[0].outEdges.size() == 1);
    REQUIRE(layout.rows[0].outEdges[0] == (gitgui::GraphEdge{0, 0}));
    REQUIRE(layout.rows[0].passThroughs.empty());

    // a: line arrives from above
    REQUIRE(layout.rows[1].lineFromAbove);
    REQUIRE(layout.rows[1].outEdges.empty());
    REQUIRE(layout.rows[1].passThroughs.empty());
}
