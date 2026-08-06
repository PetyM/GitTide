#include "gittide/diffengine.hpp"

#include <git2.h>
#include <memory>
#include <set>
#include <sstream>

namespace gittide {

Expected<DiffResult> DiffEngine::parse(git_diff* diff)
{
    DiffResult out;
    if (git_diff_num_deltas(diff) == 0)
        return out;

    git_patch* raw = nullptr;
    int rc         = git_patch_from_diff(&raw, diff, 0); // single-file diff: delta 0
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_patch, decltype(&git_patch_free)> patch(raw, git_patch_free);

    size_t nhunks = git_patch_num_hunks(patch.get());
    out.hunks.reserve(nhunks);
    for (size_t hi = 0; hi < nhunks; ++hi)
    {
        const git_diff_hunk* gh = nullptr;
        size_t nlines           = 0;
        rc                      = git_patch_get_hunk(&gh, &nlines, patch.get(), hi);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        DiffHunk hunk;
        hunk.oldStart = gh->old_start;
        hunk.oldLines = gh->old_lines;
        hunk.newStart = gh->new_start;
        hunk.newLines = gh->new_lines;
        hunk.lines.reserve(nlines);

        for (size_t li = 0; li < nlines; ++li)
        {
            const git_diff_line* gl = nullptr;
            rc                      = git_patch_get_line_in_hunk(&gl, patch.get(), hi, li);
            if (rc < 0)
                return std::unexpected(lastGitError(rc));

            // A "\ No newline at end of file" marker annotates the PREVIOUS line.
            if (gl->origin == GIT_DIFF_LINE_CONTEXT_EOFNL || gl->origin == GIT_DIFF_LINE_ADD_EOFNL ||
                gl->origin == GIT_DIFF_LINE_DEL_EOFNL)
            {
                if (!hunk.lines.empty())
                    hunk.lines.back().noNewline = true;
                continue; // do not emit a DiffLine for the marker itself
            }

            DiffLine line;
            switch (gl->origin)
            {
            case GIT_DIFF_LINE_ADDITION:
                line.origin = DiffLineOrigin::Added;
                break;
            case GIT_DIFF_LINE_DELETION:
                line.origin = DiffLineOrigin::Removed;
                break;
            default:
                line.origin = DiffLineOrigin::Context;
                break;
            }
            line.oldLineno = gl->old_lineno;
            line.newLineno = gl->new_lineno;
            // content is NOT null-terminated; strip a single trailing '\n'.
            size_t len = gl->content_len;
            if (len > 0 && gl->content[len - 1] == '\n')
                --len;
            line.text.assign(gl->content, len);
            hunk.lines.push_back(std::move(line));
        }
        out.hunks.push_back(std::move(hunk));
    }
    return out;
}

namespace {

/// One hunk rendered as patch body text, with the line counts its header needs.
struct HunkBody
{
    std::string body;
    int oldCount = 0; ///< lines present on the base being patched
    int newCount = 0; ///< lines present in the result
};

/// Render @p hunk's selected lines. @p sel.lineIndices empty takes the whole hunk;
/// @p reverse swaps added/removed so the body applies to the diff's new side.
HunkBody renderHunk(const DiffHunk& hunk, const StageSelection& sel, bool reverse)
{
    const bool whole_hunk = sel.lineIndices.empty();
    std::set<int> selected(sel.lineIndices.begin(), sel.lineIndices.end());
    auto isSelected = [&](int i)
    {
        return whole_hunk || selected.count(i) > 0;
    };

    HunkBody out;
    std::ostringstream body;
    for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i)
    {
        const DiffLine& ln    = hunk.lines[i];
        DiffLineOrigin origin = ln.origin;
        // Reverse swaps added<->removed.
        if (reverse)
        {
            if (origin == DiffLineOrigin::Added)
                origin = DiffLineOrigin::Removed;
            else if (origin == DiffLineOrigin::Removed)
                origin = DiffLineOrigin::Added;
        }

        auto emit = [&](char prefix)
        {
            body << prefix << ln.text << '\n';
            if (ln.noNewline)
                body << "\\ No newline at end of file\n";
        };

        if (origin == DiffLineOrigin::Context)
        {
            emit(' ');
            ++out.oldCount;
            ++out.newCount;
        }
        else if (origin == DiffLineOrigin::Added)
        {
            if (isSelected(i))
            {
                emit('+');
                ++out.newCount;
            }
            // unselected added line: drop entirely.
        }
        else
        { // Removed
            if (isSelected(i))
            {
                emit('-');
                ++out.oldCount;
            }
            else
            {
                emit(' ');
                ++out.oldCount;
                ++out.newCount;
            } // keep as context
        }
    }
    out.body = body.str();
    return out;
}

} // namespace

std::string buildPatch(const std::string& gitPath, const DiffResult& diff, const std::vector<StageSelection>& sels,
                       PatchOptions opt)
{
    std::ostringstream out;
    out << "diff --git a/" << gitPath << " b/" << gitPath << '\n';
    if (opt.addFile)
        out << "new file mode " << std::oct << opt.fileMode << std::dec << '\n';
    out << (opt.addFile ? "--- /dev/null\n" : "--- a/" + gitPath + "\n") << "+++ b/" << gitPath << '\n';

    // Every hunk header is numbered against the base being patched. Earlier hunks
    // in this buffer shift the result's line numbers, so carry their net delta.
    int delta = 0;
    for (const StageSelection& sel : sels)
    {
        const int hi = sel.hunkIndex.value_or(-1);
        if (hi < 0 || hi >= static_cast<int>(diff.hunks.size()))
            continue;
        const DiffHunk& hunk = diff.hunks[hi];
        const HunkBody h     = renderHunk(hunk, sel, opt.reverse);
        // Reverse applies to the diff's new side, so that side supplies the start.
        const int baseStart = opt.reverse ? hunk.newStart : hunk.oldStart;
        out << "@@ -" << baseStart << ',' << h.oldCount << " +" << (baseStart + delta) << ',' << h.newCount << " @@\n" << h.body;
        delta += h.newCount - h.oldCount;
    }
    return out.str();
}

std::string buildPatch(const std::string& gitPath, const DiffHunk& hunk, const StageSelection& sel, bool reverse)
{
    DiffResult one;
    one.hunks.push_back(hunk);
    StageSelection only = sel;
    only.hunkIndex      = 0;
    return buildPatch(gitPath, one, {only}, PatchOptions{.reverse = reverse});
}

} // namespace gittide
