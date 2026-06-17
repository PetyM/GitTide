// core/src/GraphBuilder.cpp
#include "gitgui/GraphBuilder.hpp"
#include <algorithm>
#include <string>

namespace gitgui {

namespace {

// Index of first slot containing oid, or -1 if not found.
int lane_of(const std::vector<std::string>& active, const std::string& oid) {
    for (int i = 0; i < static_cast<int>(active.size()); ++i)
        if (active[i] == oid) return i;
    return -1;
}

// Index of first empty ("") slot, appending one if needed.
int alloc_lane(std::vector<std::string>& active) {
    for (int i = 0; i < static_cast<int>(active.size()); ++i)
        if (active[i].empty()) return i;
    active.push_back("");
    return static_cast<int>(active.size()) - 1;
}

}  // namespace

GraphLayout GraphBuilder::build(std::vector<CommitNode> commits) {
    GraphLayout layout;
    if (commits.empty()) return layout;

    std::vector<std::string> active;  // active[i] = OID expected at lane i
    int max_lane = 0;

    for (auto& commit : commits) {
        // 1. Find or assign this commit's lane.
        int cl = lane_of(active, commit.oid);
        const bool was_in_active = (cl >= 0);
        if (!was_in_active) {
            cl = alloc_lane(active);
        }
        commit.lane = cl;
        max_lane = std::max(max_lane, cl);

        // 2. Update active: first parent inherits this lane, extra parents get new lanes.
        if (commit.parents.empty()) {
            active[cl] = "";
        } else {
            active[cl] = commit.parents[0];
        }
        for (std::size_t pi = 1; pi < commit.parents.size(); ++pi) {
            if (lane_of(active, commit.parents[pi]) < 0) {
                int slot = alloc_lane(active);
                active[slot] = commit.parents[pi];
                max_lane = std::max(max_lane, slot);
            }
        }

        // 3. outEdges: for each parent, find its lane now in active.
        std::vector<GraphEdge> out;
        for (const auto& p : commit.parents) {
            int pl = lane_of(active, p);
            if (pl >= 0)
                out.push_back({cl, pl});
        }

        // 4. passThroughs: every non-empty active lane that is not this commit's lane.
        std::vector<int> pass;
        for (int i = 0; i < static_cast<int>(active.size()); ++i)
            if (!active[i].empty() && i != cl)
                pass.push_back(i);

        layout.rows.push_back(GraphRow{
            std::move(commit),
            was_in_active,
            std::move(pass),
            std::move(out),
        });
    }

    layout.laneCount = max_lane + 1;
    return layout;
}

}  // namespace gitgui
