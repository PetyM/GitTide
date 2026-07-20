#pragma once
#include <filesystem>
#include <memory>
#include <qcorotask.h>
#include <string>
#include <vector>

#include <QString>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/giterror.hpp"
#include "gittide/gitrepo.hpp" // ProgressCallback
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"
#include "gittide/rebase.hpp"
#include "gittide/submodule.hpp"
#include "gittide/sync.hpp"

namespace gittide::ui {

// Async wrapper over a single GitRepo. Each call runs on Qt's global thread pool
// via QtConcurrent and is exposed as a co_await-able QCoro task. A per-repo mutex
// (held inside the worker lambda) serializes pool access so two awaited ops never
// touch the same git_repository concurrently — satisfying Core's one-owner rule.
//
// Move-only. The GitRepo + mutex live behind a shared_ptr so in-flight work stays
// valid even if the AsyncRepo is destroyed before the task completes.
class AsyncRepo
{
public:
    static gittide::Expected<AsyncRepo> open(const std::filesystem::path& path);

    AsyncRepo(AsyncRepo&&) noexcept            = default;
    AsyncRepo& operator=(AsyncRepo&&) noexcept = default;
    AsyncRepo(const AsyncRepo&)                = delete;
    AsyncRepo& operator=(const AsyncRepo&)     = delete;
    ~AsyncRepo();

    QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> status();

    /// Directories to watch to keep this repo's view current (D35). See
    /// gittide::WatchTargets and GitRepo::watchTargets.
    QCoro::Task<gittide::Expected<gittide::WatchTargets>> watchTargets();
    QCoro::Task<gittide::Expected<gittide::DiffResult>> diff(gittide::DiffTarget target, std::filesystem::path file);
    QCoro::Task<gittide::Expected<void>> stage(gittide::StageSelection sel);
    QCoro::Task<gittide::Expected<void>> unstage(gittide::StageSelection sel);
    QCoro::Task<gittide::Expected<void>> discard(gittide::StageSelection sel);
    QCoro::Task<gittide::Expected<void>> discardAll();
    QCoro::Task<gittide::Expected<std::string>> commit(gittide::CommitRequest req);
    QCoro::Task<gittide::Expected<std::vector<gittide::CommitNode>>> log(unsigned limit = 1000);
    QCoro::Task<gittide::Expected<std::vector<gittide::CommitNode>>> logAllRefs(unsigned limit = 1000);
    QCoro::Task<gittide::Expected<std::vector<gittide::RefTip>>>     refTips();
    QCoro::Task<gittide::Expected<std::vector<std::string>>>         localOnlyOids();

    QCoro::Task<gittide::Expected<void>> resetIndexToHead();
    QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> commitFiles(QString oid);
    QCoro::Task<gittide::Expected<gittide::DiffResult>>              commitDiff(QString oid, std::filesystem::path file);

    QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> rangeFiles(QString oldOid, QString newOid);
    QCoro::Task<gittide::Expected<gittide::DiffResult>>              rangeDiff(QString oldOid, QString newOid, std::filesystem::path file);
    QCoro::Task<gittide::Expected<std::string>>                      rewordHead(QString message);
    /// Undo the last commit (soft reset to its first parent; changes stay staged).
    QCoro::Task<gittide::Expected<void>>                             undoLastCommit();
    QCoro::Task<gittide::Expected<std::string>>                      commitMessage(QString oid);

    /// First-parent oid (40-char hex) of `oid`. Errors if `oid` is a root commit.
    QCoro::Task<gittide::Expected<std::string>> firstParent(QString oid);

    QCoro::Task<gittide::Expected<std::vector<gittide::BranchInfo>>> branches();
    QCoro::Task<gittide::Expected<gittide::HeadState>>               head();
    QCoro::Task<gittide::Expected<void>> createBranch(QString name, QString fromOid);
    QCoro::Task<gittide::Expected<void>> checkoutBranch(QString name);
    QCoro::Task<gittide::Expected<void>> checkoutRemoteBranch(QString remoteShorthand);
    QCoro::Task<gittide::Expected<void>> checkoutCommit(QString oid);
    QCoro::Task<gittide::Expected<void>> deleteBranch(QString name, bool force);
    QCoro::Task<gittide::Expected<void>> renameBranch(QString oldName, QString newName, bool force);

    QCoro::Task<gittide::Expected<gittide::SyncStatus>> syncStatus();
    QCoro::Task<gittide::Expected<std::string>>         remoteUrl(QString remote);
    QCoro::Task<gittide::Expected<void>>                fetch(QString remote, gittide::Credentials cred, gittide::ProgressCallback onProgress);
    QCoro::Task<gittide::Expected<void>>                pull(gittide::Credentials cred, gittide::ProgressCallback onProgress);
    QCoro::Task<gittide::Expected<void>>                push(QString remote, QString branch, bool setUpstream, gittide::Credentials cred, gittide::ProgressCallback onProgress);
    QCoro::Task<gittide::Expected<gittide::PullStrategy>> pullStrategy();
    QCoro::Task<gittide::Expected<void>>                setPullStrategy(gittide::PullStrategy strategy);

    // Identity (user.name / user.email) — see GitRepo. setLocalIdentity records the
    // gittide.identity ownership marker; clearLocalIdentity drops it so the repo
    // falls back to global; setGlobalIdentity writes ~/.gitconfig.
    QCoro::Task<gittide::Expected<void>>                     setLocalIdentity(QString name, QString email, QString marker);
    QCoro::Task<gittide::Expected<void>>                     clearLocalIdentity();
    QCoro::Task<gittide::Expected<gittide::LocalIdentityInfo>> localIdentity();
    QCoro::Task<gittide::Expected<gittide::ConfigIdentity>>    effectiveIdentity();

    /// Analyse and perform a merge of the named branch into the current HEAD.
    /// Returns the merge outcome (conflicted status, analysis type, new OID if clean/FF).
    QCoro::Task<gittide::Expected<gittide::MergeOutcome>> mergeBranch(QString name);

    /// Retrieve the current merge-in-progress state (derived from repository state,
    /// MERGE_MSG, and conflict entries). Always current, never cached.
    QCoro::Task<gittide::Expected<gittide::MergeState>> mergeState();

    /// Create the merge commit from the current index during a merge.
    /// Clears merge state on success. Errors if unresolved conflicts remain.
    QCoro::Task<gittide::Expected<std::string>> commitMerge(gittide::CommitRequest req);

    /// Abort an in-progress merge: reset working tree to HEAD and clear merge state.
    QCoro::Task<gittide::Expected<void>> abortMerge();

    /// Rebase the current branch onto ontoRef's tip (clean tree assumed; controller stashes).
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> startRebase(QString ontoRef);
    /// Begin an interactive rebase (clean tree assumed; controller stashes).
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> startInteractiveRebase(gittide::RebaseTodo todo);
    /// Continue an in-progress rebase. `message` is non-empty only for an
    /// interactive Message pause (reword/squash); empty otherwise.
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> continueRebase(QString message = QString());
    /// Skip the current rebase step.
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> skipRebase();
    /// Abort an in-progress rebase, restoring the pre-rebase state.
    QCoro::Task<gittide::Expected<void>> abortRebase();
    /// Rebase-in-progress state, derived from disk (D30).
    QCoro::Task<gittide::Expected<gittide::RebaseState>> rebaseState();

    /// Stash the working tree if dirty. Returns true if a stash was created,
    /// false if the tree was already clean.
    QCoro::Task<gittide::Expected<bool>> stashSave(QString message);

    /// Pop the most-recent stash onto the working tree.
    /// Errors (and preserves the stash) if the pop conflicts.
    QCoro::Task<gittide::Expected<void>> stashPop();

    /// Number of entries on the stash stack.
    QCoro::Task<gittide::Expected<int>> stashCount();

    /// Enumerate the stash stack (newest first).
    QCoro::Task<gittide::Expected<std::vector<gittide::StashEntry>>> stashList();
    /// Apply stash@{index}, keeping it on the stack. Conflict → error, stash kept.
    QCoro::Task<gittide::Expected<void>> stashApplyAt(int index);
    /// Apply stash@{index} and drop it. Conflict → error, stash kept.
    QCoro::Task<gittide::Expected<void>> stashPopAt(int index);
    /// Drop stash@{index} without applying it.
    QCoro::Task<gittide::Expected<void>> stashDrop(int index);
    /// Drop every stash entry.
    QCoro::Task<gittide::Expected<void>> stashClear();

    /// Files in stash commit @p oid (tracked changes + untracked), for the preview.
    QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> stashFiles(QString oid);
    /// Diff of one file within stash commit @p oid (tracked or untracked).
    QCoro::Task<gittide::Expected<gittide::DiffResult>> stashDiff(QString oid, std::filesystem::path file);

    /// De-initialise a submodule: remove its working-tree source files while
    /// preserving the .git gitlink file. path is repo-relative.
    QCoro::Task<gittide::Expected<void>> deinitSubmodule(std::filesystem::path path);

    /// Re-initialise and update a submodule to its pinned commit.
    /// path is repo-relative.
    QCoro::Task<gittide::Expected<void>> reinitSubmodule(std::filesystem::path path);

    /// Initialise/update every direct submodule to its pinned commit (one level).
    QCoro::Task<gittide::Expected<void>> updateSubmodules();

    /// Enumerate this repo's recursive submodule tree off the UI thread.
    QCoro::Task<gittide::Expected<std::vector<gittide::SubmoduleNode>>> submoduleTree();

private:
    struct Impl;
    explicit AsyncRepo(std::shared_ptr<Impl> impl)
        : m_impl(std::move(impl))
    {
    }
    std::shared_ptr<Impl> m_impl;
};

} // namespace gittide::ui
