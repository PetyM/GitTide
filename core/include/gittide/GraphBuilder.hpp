// core/include/gittide/GraphBuilder.hpp
#pragma once
#include "gittide/Graph.hpp"
#include <vector>

namespace gittide {

class GraphBuilder {
public:
    // Assign lane indices and compute per-row passthrough/edge metadata.
    // commits must be in topological walk order (newest first).
    static GraphLayout build(std::vector<CommitNode> commits);
};

}  // namespace gittide
