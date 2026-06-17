// core/include/gittide/graphbuilder.hpp
#pragma once
#include "gittide/graph.hpp"
#include <vector>

namespace gittide {

class GraphBuilder {
public:
    // Assign lane indices and compute per-row passthrough/edge metadata.
    // commits must be in topological walk order (newest first).
    static GraphLayout build(std::vector<CommitNode> commits);
};

}  // namespace gittide
