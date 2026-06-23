#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

/// git_merge_analysis result, reduced to the cases GitTide acts on.
enum class MergeAnalysis
{
    UpToDate,    ///< nothing to merge
    FastForward, ///< HEAD can be fast-forwarded to the target
    Normal,      ///< a real merge (may or may not conflict)
};

/// Result of starting a merge (mergeBranch).
struct MergeOutcome
{
    MergeAnalysis analysis   = MergeAnalysis::UpToDate;
    bool          conflicted = false; ///< Normal merge that left conflict entries
    std::string   newOid;             ///< FF/clean: the new HEAD/merge-commit oid; else empty
};

/// Merge-in-progress state, ALWAYS derived from the repository (D30).
struct MergeState
{
    bool        inProgress = false; ///< git_repository_state == MERGE
    std::string mergedRef;          ///< e.g. "feature/x", parsed from MERGE_MSG; may be empty
    std::vector<std::filesystem::path> conflictedPaths;       ///< all conflicted entries
    std::vector<std::filesystem::path> conflictedSubmodules;  ///< gitlink subset of the above
};

} // namespace gittide
