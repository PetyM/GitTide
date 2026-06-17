#include "gitgui/DiffEngine.hpp"
#include <git2.h>
#include <memory>

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

}  // namespace gitgui
