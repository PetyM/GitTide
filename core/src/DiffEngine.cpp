#include "gitgui/DiffEngine.hpp"
#include <git2.h>
#include <memory>
#include <set>
#include <sstream>

namespace gitgui {

Expected<DiffResult> DiffEngine::parse(git_diff* diff) {
    DiffResult out;
    if (git_diff_num_deltas(diff) == 0) return out;

    git_patch* raw = nullptr;
    int rc = git_patch_from_diff(&raw, diff, 0);  // single-file diff: delta 0
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_patch, decltype(&git_patch_free)> patch(raw, git_patch_free);

    size_t nhunks = git_patch_num_hunks(patch.get());
    out.hunks.reserve(nhunks);
    for (size_t hi = 0; hi < nhunks; ++hi) {
        const git_diff_hunk* gh = nullptr;
        size_t nlines = 0;
        rc = git_patch_get_hunk(&gh, &nlines, patch.get(), hi);
        if (rc < 0) return std::unexpected(last_git_error(rc));

        DiffHunk hunk;
        hunk.oldStart = gh->old_start; hunk.oldLines = gh->old_lines;
        hunk.newStart = gh->new_start; hunk.newLines = gh->new_lines;
        hunk.lines.reserve(nlines);

        for (size_t li = 0; li < nlines; ++li) {
            const git_diff_line* gl = nullptr;
            rc = git_patch_get_line_in_hunk(&gl, patch.get(), hi, li);
            if (rc < 0) return std::unexpected(last_git_error(rc));

            DiffLine line;
            switch (gl->origin) {
                case GIT_DIFF_LINE_ADDITION: line.origin = DiffLineOrigin::Added; break;
                case GIT_DIFF_LINE_DELETION: line.origin = DiffLineOrigin::Removed; break;
                default:                     line.origin = DiffLineOrigin::Context; break;
            }
            line.oldLineno = gl->old_lineno;
            line.newLineno = gl->new_lineno;
            // content is NOT null-terminated; strip a single trailing '\n'.
            size_t len = gl->content_len;
            if (len > 0 && gl->content[len - 1] == '\n') --len;
            line.text.assign(gl->content, len);
            hunk.lines.push_back(std::move(line));
        }
        out.hunks.push_back(std::move(hunk));
    }
    return out;
}

std::string build_patch(const std::string& gitPath,
                        const DiffHunk& hunk,
                        const StageSelection& sel,
                        bool reverse) {
    const bool whole_hunk = sel.lineIndices.empty();
    std::set<int> selected(sel.lineIndices.begin(), sel.lineIndices.end());
    auto is_selected = [&](int i) { return whole_hunk || selected.count(i) > 0; };

    // Build the body and count old/new lines as we go.
    std::ostringstream body;
    int oldCount = 0, newCount = 0;
    for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i) {
        const DiffLine& ln = hunk.lines[i];
        DiffLineOrigin origin = ln.origin;
        // Reverse swaps added<->removed.
        if (reverse) {
            if (origin == DiffLineOrigin::Added)        origin = DiffLineOrigin::Removed;
            else if (origin == DiffLineOrigin::Removed) origin = DiffLineOrigin::Added;
        }

        if (origin == DiffLineOrigin::Context) {
            body << ' ' << ln.text << '\n';
            ++oldCount; ++newCount;
        } else if (origin == DiffLineOrigin::Added) {
            if (is_selected(i)) { body << '+' << ln.text << '\n'; ++newCount; }
            // unselected added line: drop entirely.
        } else {  // Removed
            if (is_selected(i)) { body << '-' << ln.text << '\n'; ++oldCount; }
            else { body << ' ' << ln.text << '\n'; ++oldCount; ++newCount; }  // keep as context
        }
    }

    std::ostringstream out;
    out << "diff --git a/" << gitPath << " b/" << gitPath << '\n'
        << "--- a/" << gitPath << '\n'
        << "+++ b/" << gitPath << '\n'
        << "@@ -" << hunk.oldStart << ',' << oldCount
        << " +" << hunk.newStart << ',' << newCount << " @@\n"
        << body.str();
    return out.str();
}

}  // namespace gitgui
