#pragma once
#include "gittide/diff.hpp"
#include "gittide/giterror.hpp"

struct git_diff;

namespace gittide {

class DiffEngine {
public:
    // Parse a SINGLE-file libgit2 diff (delta 0) into hunks and lines.
    // Returns an empty DiffResult (no hunks) if the diff has no deltas.
    static Expected<DiffResult> parse(git_diff* diff);
};

// Serialize a selection within a single hunk to a minimal unified-diff buffer
// suitable for git_apply. `gitPath` is the libgit2 (forward-slash) path.
// If sel.lineIndices is empty, the whole hunk is taken.
std::string build_patch(const std::string& gitPath,
                        const DiffHunk& hunk,
                        const StageSelection& sel,
                        bool reverse);

}  // namespace gittide
