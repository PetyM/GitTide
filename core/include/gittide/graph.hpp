#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gittide {

// A single commit as returned by GitRepo::log. lane is filled by GraphBuilder.
struct CommitNode {
    std::string oid;      // full SHA-1 hex
    std::string summary;  // first line of commit message
    std::string author;   // author name
    int64_t     time = 0; // author unix timestamp
    std::vector<std::string> parents;  // parent OIDs (empty = initial commit)
    int lane = 0;         // column index; 0 = leftmost; filled by GraphBuilder
};

// A directed edge in the graph: line from fromLane (this row) to toLane (next row).
struct GraphEdge {
    int fromLane = 0;
    int toLane   = 0;
    bool operator==(const GraphEdge&) const = default;
};

// One row of the rendered graph. The delegate needs all three fields to draw
// the top half (lineFromAbove), the dot (commit.lane), and the bottom half (outEdges).
struct GraphRow {
    CommitNode commit;

    // Draw a vertical line from the top of this cell to the circle?
    // True when a previous commit had an outEdge pointing to this commit's lane.
    // False for branch heads (first occurrence of this commit in the walk).
    bool lineFromAbove = false;

    // Lane indices that carry an unbroken vertical line through this entire row
    // (neither starting nor ending here). Never includes commit.lane.
    std::vector<int> passThroughs;

    // Lines from this commit's circle down to each parent's lane (bottom half).
    // fromLane is always commit.lane. toLane is the parent's assigned lane.
    std::vector<GraphEdge> outEdges;
};

struct GraphLayout {
    std::vector<GraphRow> rows;
    int laneCount = 0;  // max lane index used + 1
};

}  // namespace gittide
