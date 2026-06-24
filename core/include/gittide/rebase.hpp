#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

/// Result of advancing a rebase (start/continue/skip).
struct RebaseOutcome
{
    bool conflicted = false; ///< true => paused on a conflicting step; false => finished
};

/// Rebase-in-progress state, ALWAYS derived from the repository (D30).
struct RebaseState
{
    bool        inProgress = false; ///< git_repository_state is REBASE / REBASE_MERGE
    std::string ontoRef;            ///< target branch shorthand (from rebase-merge/onto_name); may be empty
    int         current = 0;        ///< current step, 1-based (0 when none)
    int         total   = 0;        ///< total steps
    std::string stepSummary;        ///< summary of the commit being applied; may be empty
    std::vector<std::filesystem::path> conflictedPaths;       ///< all conflicted entries
    std::vector<std::filesystem::path> conflictedSubmodules;  ///< gitlink subset of the above
};

} // namespace gittide
