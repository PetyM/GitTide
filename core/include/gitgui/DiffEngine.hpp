#pragma once
#include "gitgui/Diff.hpp"
#include "gitgui/GitError.hpp"

struct git_diff;

namespace gitgui {

class DiffEngine {
public:
    // Parse a SINGLE-file libgit2 diff (delta 0) into hunks and lines.
    // Returns an empty DiffResult (no hunks) if the diff has no deltas.
    static Expected<DiffResult> parse(git_diff* diff);
};

}  // namespace gitgui
