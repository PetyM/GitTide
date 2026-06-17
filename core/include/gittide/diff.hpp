#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gittide {

// Which pair of trees a diff compares.
enum class DiffTarget
{
    WorktreeVsIndex, // unstaged changes
    IndexVsHead,     // staged changes
};

enum class DiffLineOrigin
{
    Context,
    Added,
    Removed
};

struct DiffLine
{
    DiffLineOrigin origin = DiffLineOrigin::Context;
    int oldLineno         = -1; // -1 when the line does not exist on that side
    int newLineno         = -1;
    std::string text;       // line content WITHOUT trailing newline
    bool noNewline = false; // this line has no trailing newline at end of file
};

struct DiffHunk
{
    int oldStart = 0, oldLines = 0;
    int newStart = 0, newLines = 0;
    std::vector<DiffLine> lines;
};

struct DiffResult
{
    std::vector<DiffHunk> hunks;
};

// A selection within ONE file.
//   hunkIndex == nullopt              -> whole file
//   hunkIndex set, lineIndices empty  -> the whole hunk
//   hunkIndex set, lineIndices filled -> those line indices within the hunk
// lineIndices are indices into DiffHunk::lines.
struct StageSelection
{
    std::filesystem::path path; // repo-relative
    std::optional<int> hunkIndex;
    std::vector<int> lineIndices;
};

struct CommitRequest
{
    std::string message;
    // author / committer come from git config (git_signature_default).
};

} // namespace gittide
