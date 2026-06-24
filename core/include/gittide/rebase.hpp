#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

/// A single interactive-rebase instruction.
enum class RebaseAction
{
    Pick,    ///< replay the commit as-is
    Reword,  ///< replay, then pause to edit the message
    Squash,  ///< fold into the previous commit, pause to edit the combined message
    Fixup,   ///< fold into the previous commit, keep the previous message (no pause)
    Drop     ///< discard the commit
};

/// Why an interactive rebase is paused (or None when running / finished).
enum class RebasePause
{
    None,
    Conflict,  ///< the current cherry-pick left conflicts to resolve
    Message    ///< a reword/squash step needs a new/combined message
};

struct RebaseTodoEntry
{
    RebaseAction action = RebaseAction::Pick;
    std::string  oid;        ///< 40-char hex of the original commit
};

/// An interactive plan: replay `entries` (in list order) on top of `base`.
struct RebaseTodo
{
    std::string                  base;     ///< oid to detach onto (parent-of-oldest)
    std::vector<RebaseTodoEntry> entries;  ///< oldest first (git todo order)
};

/// Result of advancing a rebase (start/continue/skip).
struct RebaseOutcome
{
    bool        conflicted = false;             ///< true => paused on a conflicting step
    RebasePause pause      = RebasePause::None; ///< why we paused (or None when finished)
};

/// Rebase-in-progress state, ALWAYS derived from the repository (D30).
struct RebaseState
{
    bool        inProgress  = false; ///< a plain (libgit2) OR interactive (our dir) rebase is live
    bool        interactive = false; ///< true => driven by the manual cherry-pick engine
    RebasePause pause       = RebasePause::None;
    std::string ontoRef;             ///< plain driver: target branch shorthand; empty for interactive
    int         current = 0;         ///< current step, 1-based (0 when none)
    int         total   = 0;         ///< total steps (interactive: non-drop entries)
    std::string stepSummary;         ///< summary of the commit being applied; may be empty
    std::string messagePrefill;      ///< pause == Message: prefilled editor text
    std::vector<std::filesystem::path> conflictedPaths;       ///< all conflicted entries
    std::vector<std::filesystem::path> conflictedSubmodules;  ///< gitlink subset of the above
};

} // namespace gittide
