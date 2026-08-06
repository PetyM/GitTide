#pragma once
#include "gittide/diff.hpp"
#include "gittide/giterror.hpp"

struct git_diff;

namespace gittide {

class DiffEngine
{
public:
    // Parse a SINGLE-file libgit2 diff (delta 0) into hunks and lines.
    // Returns an empty DiffResult (no hunks) if the diff has no deltas.
    static Expected<DiffResult> parse(git_diff* diff);
};

/// How a patch buffer is framed for git_apply.
struct PatchOptions
{
    /// Swap '+' and '-' — the patch then applies to the diff's NEW side (unstage,
    /// discard) instead of its old side.
    bool reverse = false;
    /// The base the patch applies to has no such file yet (an untracked file being
    /// staged): frame it as an addition ("new file mode" + `--- /dev/null`).
    /// Without it git_apply fails with "index does not contain '<path>'".
    bool addFile = false;
    /// Mode recorded by the "new file mode" line; only read when addFile is set.
    unsigned fileMode = 0100644;
};

/// Serialize hunk selections within ONE file to a minimal unified-diff buffer
/// suitable for git_apply. `gitPath` is the libgit2 (forward-slash) path.
///
/// Every entry of @p sels must carry a hunkIndex into @p diff; they must be in
/// ascending hunk order and free of duplicates. An entry with empty lineIndices
/// takes its whole hunk. All selected hunks land in ONE buffer so a multi-hunk
/// selection applies against a single diff snapshot — applying them one at a time
/// would invalidate the later hunk indices (see GitRepo::stage).
///
/// Hunk headers are numbered against the base being patched: the old-side start
/// is the hunk's start on that base, the new-side start is shifted by the net line
/// delta of the hunks emitted before it in this same buffer.
std::string buildPatch(const std::string& gitPath, const DiffResult& diff, const std::vector<StageSelection>& sels,
                       PatchOptions opt = {});

/// Single-hunk convenience overload: patches @p hunk alone, honouring
/// sel.lineIndices (empty = the whole hunk).
std::string buildPatch(const std::string& gitPath, const DiffHunk& hunk, const StageSelection& sel, bool reverse);

} // namespace gittide
