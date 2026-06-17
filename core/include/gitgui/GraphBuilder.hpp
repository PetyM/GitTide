// core/include/gitgui/GraphBuilder.hpp
#pragma once
#include "gitgui/Graph.hpp"
#include <vector>

namespace gitgui {

class GraphBuilder {
public:
    // Assign lane indices and compute per-row passthrough/edge metadata.
    // commits must be in topological walk order (newest first).
    static GraphLayout build(std::vector<CommitNode> commits);
};

}  // namespace gitgui
