#pragma once
#include <map>
#include <vector>

#include <QHash>
#include <QSet>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <qcorotask.h>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"
#include "gittide/rebase.hpp"
#include "gittide/sync.hpp"
#include "gittide/ui/branchlistmodel.hpp"
#include "gittide/ui/changedfilesmodel.hpp"
#include "gittide/ui/difflinesmodel.hpp"
#include "gittide/ui/historylistmodel.hpp"
#include "gittide/ui/stashlistmodel.hpp"

namespace gittide::ui {

class RepoController;
class CredentialManager;

/// QML-facing façade over the signal-driven RepoController. Owns the controller,
/// the two list models QML binds to, and the staging-selection state (whole-file
/// tri-state per path + per-hunk checked line indices). Kicks the controller's
/// QCoro::Task methods on behalf of QML (which cannot call them) and translates
/// its std::-typed signals into model updates and property changes. Staging/commit
/// semantics replicate ui/src/changesview.cpp.
class RepoViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool repoOpen READ repoOpen NOTIFY changed)
    /// True when the working tree has any changes (staged or unstaged). Drives
    /// enable/disable of the "discard all" action. Re-evaluated on every status
    /// refresh via the dedicated dirtyChanged() notify, which onStatus() emits.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    /// True when the stash stack is non-empty. Drives enable/visibility of the
    /// "pop stash" action. Re-evaluated whenever the controller reports a new
    /// stash count (on open, git-dir changes, and after stash save/pop).
    Q_PROPERTY(bool stashAvailable READ stashAvailable NOTIFY stashCountChanged)
    /// Path of the open repository (empty when none). The sidebar marks the row
    /// whose repoPath matches this as the active repo.
    Q_PROPERTY(QString repoPath READ repoPath NOTIFY changed)
    Q_PROPERTY(QString currentBranch READ currentBranch NOTIFY branchChanged)
    Q_PROPERTY(QString activeFile READ activeFile NOTIFY activeFileChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY checkedChanged)
    Q_PROPERTY(gittide::ui::ChangedFilesModel* changedFiles READ changedFiles CONSTANT)
    Q_PROPERTY(gittide::ui::DiffLinesModel* diffLines READ diffLines CONSTANT)
    Q_PROPERTY(gittide::ui::BranchListModel* branches READ branches CONSTANT)
    Q_PROPERTY(gittide::ui::HistoryListModel* history READ history CONSTANT)
    Q_PROPERTY(gittide::ui::HistoryListModel* graph READ graph CONSTANT)
    Q_PROPERTY(gittide::ui::ChangedFilesModel* commitFiles READ commitFiles CONSTANT)
    Q_PROPERTY(gittide::ui::DiffLinesModel* commitDiff READ commitDiff CONSTANT)
    Q_PROPERTY(gittide::ui::StashListModel* stashes READ stashes CONSTANT)
    Q_PROPERTY(bool stashPreviewActive READ stashPreviewActive NOTIFY stashPreviewChanged)
    Q_PROPERTY(QString stashPreviewLabel READ stashPreviewLabel NOTIFY stashPreviewChanged)
    /// Row index of the stash currently being previewed, or -1 when no preview is
    /// active. Lets the panel highlight the previewed row and toggle it off on a
    /// second click (the primary "get out of preview" affordance).
    Q_PROPERTY(int stashPreviewIndex READ stashPreviewIndex NOTIFY stashPreviewChanged)
    Q_PROPERTY(QString selectedCommit READ selectedCommit NOTIFY selectedCommitChanged)
    Q_PROPERTY(QString detailSummary READ detailSummary NOTIFY commitDetailChanged)
    Q_PROPERTY(QString detailBody READ detailBody NOTIFY commitDetailChanged)
    Q_PROPERTY(QString detailAuthor READ detailAuthor NOTIFY commitDetailChanged)
    Q_PROPERTY(QString detailAuthorEmail READ detailAuthorEmail NOTIFY commitDetailChanged)
    Q_PROPERTY(QString detailDate READ detailDate NOTIFY commitDetailChanged)
    Q_PROPERTY(int detailFilesChanged READ detailFilesChanged NOTIFY commitDetailChanged)
    Q_PROPERTY(int detailAdditions READ detailAdditions NOTIFY commitDetailChanged)
    Q_PROPERTY(int detailDeletions READ detailDeletions NOTIFY commitDetailChanged)
    Q_PROPERTY(QString activeCommitFile READ activeCommitFile NOTIFY activeCommitFileChanged)
    Q_PROPERTY(QString historyDetailHeader READ historyDetailHeader NOTIFY historyDetailChanged)
    Q_PROPERTY(QString historyDetailHint READ historyDetailHint NOTIFY historyDetailChanged)
    // Number of newest commits (from HEAD down) that form a reorderable run: a
    // contiguous span of single-parent (non-merge) commits. < 2 means no drag
    // reorder is offered. Drives which history rows are draggable.
    Q_PROPERTY(int reorderableRunLength READ reorderableRunLength NOTIFY reorderableRunChanged)
    Q_PROPERTY(int aheadCount READ aheadCount NOTIFY syncStatusChanged)
    Q_PROPERTY(int behindCount READ behindCount NOTIFY syncStatusChanged)
    Q_PROPERTY(bool hasUpstream READ hasUpstream NOTIFY syncStatusChanged)
    Q_PROPERTY(QString upstreamName READ upstreamName NOTIFY syncStatusChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    // Transfer progress of the in-flight sync: fraction 0..1, or -1 when the
    // total is not yet known (show an indeterminate bar). received/total feed a
    // "received / total" caption.
    Q_PROPERTY(qreal syncProgress READ syncProgress NOTIFY syncProgressChanged)
    Q_PROPERTY(int syncReceived READ syncReceived NOTIFY syncProgressChanged)
    Q_PROPERTY(int syncTotal READ syncTotal NOTIFY syncProgressChanged)
    Q_PROPERTY(bool pullRebase READ pullRebase NOTIFY pullRebaseChanged)
    Q_PROPERTY(bool onBranch READ onBranch NOTIFY branchChanged)
    /// True while a merge is in progress (conflict state or awaiting commit).
    Q_PROPERTY(bool mergeInProgress READ mergeInProgress NOTIFY mergeStateChanged)
    /// The ref name being merged into HEAD (e.g. "feature"); empty when none.
    Q_PROPERTY(QString mergedRef READ mergedRef NOTIFY mergeStateChanged)
    /// Number of files with merge conflicts that still need resolution.
    Q_PROPERTY(int conflictedCount READ conflictedCount NOTIFY mergeStateChanged)
    /// True when at least one conflicted file is a submodule (gitlink).
    Q_PROPERTY(bool hasSubmoduleConflicts READ hasSubmoduleConflicts NOTIFY mergeStateChanged)
    /// True while a rebase is in progress (conflict state or advancing).
    Q_PROPERTY(bool    rebaseInProgress  READ rebaseInProgress  NOTIFY rebaseStateChanged)
    /// The ref name being rebased onto (e.g. "master"); empty when none.
    Q_PROPERTY(QString rebaseOnto        READ rebaseOnto        NOTIFY rebaseStateChanged)
    /// Current rebase step, 1-based (0 when none).
    Q_PROPERTY(int     rebaseStep        READ rebaseStep        NOTIFY rebaseStateChanged)
    /// Total number of rebase steps.
    Q_PROPERTY(int     rebaseTotal       READ rebaseTotal       NOTIFY rebaseStateChanged)
    /// Summary of the commit being applied at the current step; empty when none.
    Q_PROPERTY(QString rebaseStepSummary READ rebaseStepSummary NOTIFY rebaseStateChanged)
    /// Number of files with rebase conflicts that still need resolution.
    Q_PROPERTY(int     rebaseConflictedCount       READ rebaseConflictedCount       NOTIFY rebaseStateChanged)
    /// True when at least one conflicted file is a submodule (gitlink).
    Q_PROPERTY(bool    rebaseHasSubmoduleConflicts READ rebaseHasSubmoduleConflicts NOTIFY rebaseStateChanged)
    /// True while the in-progress rebase is interactive (manual engine).
    Q_PROPERTY(bool    rebaseInteractive    READ rebaseInteractive    NOTIFY rebaseStateChanged)
    /// Pause reason: "none" | "conflict" | "message".
    Q_PROPERTY(QString rebasePauseReason    READ rebasePauseReason    NOTIFY rebaseStateChanged)
    /// Prefilled text for a Message pause (reword/squash); empty otherwise.
    Q_PROPERTY(QString rebaseMessagePrefill READ rebaseMessagePrefill NOTIFY rebaseStateChanged)

public:
    explicit RepoViewModel(QObject* parent = nullptr);

    bool repoOpen() const;
    bool dirty() const;
    bool stashAvailable() const { return m_stashCount > 0; }
    QString repoPath() const;
    QString currentBranch() const;
    QString activeFile() const;
    int checkedCount() const;
    ChangedFilesModel* changedFiles() const;
    DiffLinesModel* diffLines() const;
    BranchListModel*    branches() const;
    HistoryListModel*   history() const;
    HistoryListModel*   graph() const;
    ChangedFilesModel* commitFiles() const;
    DiffLinesModel* commitDiff() const;
    StashListModel* stashes() const { return m_stashes; }
    bool stashPreviewActive() const { return m_stashPreviewActive; }
    QString stashPreviewLabel() const { return m_stashPreviewLabel; }
    int stashPreviewIndex() const { return m_stashPreviewIndex; }
    QString selectedCommit() const;
    QString detailSummary() const { return m_detailSummary; }
    QString detailBody() const { return m_detailBody; }
    QString detailAuthor() const { return m_detailAuthor; }
    QString detailAuthorEmail() const { return m_detailAuthorEmail; }
    QString detailDate() const { return m_detailDate; }
    int detailFilesChanged() const { return m_detailFilesChanged; }
    int detailAdditions() const { return m_detailAdditions; }
    int detailDeletions() const { return m_detailDeletions; }
    QString activeCommitFile() const;
    QString historyDetailHeader() const { return m_detailHeader; }
    QString historyDetailHint() const { return m_detailHint; }
    int reorderableRunLength() const { return m_reorderableRunLength; }
    int aheadCount() const { return m_sync.ahead; }
    int behindCount() const { return m_sync.behind; }
    bool hasUpstream() const { return m_sync.hasUpstream; }
    QString upstreamName() const { return QString::fromStdString(m_sync.upstreamName); }
    bool syncing() const { return m_syncing; }
    qreal syncProgress() const { return m_syncTotal > 0 ? qreal(m_syncReceived) / qreal(m_syncTotal) : -1.0; }
    int syncReceived() const { return m_syncReceived; }
    int syncTotal() const { return m_syncTotal; }
    bool pullRebase() const { return m_pullRebase; }
    bool onBranch() const { return !m_headBranch.isEmpty(); }
    bool mergeInProgress() const { return m_merge.inProgress; }
    QString mergedRef() const { return QString::fromStdString(m_merge.mergedRef); }
    int conflictedCount() const { return int(m_merge.conflictedPaths.size()); }
    bool hasSubmoduleConflicts() const { return !m_merge.conflictedSubmodules.empty(); }
    bool    rebaseInProgress() const { return m_rebase.inProgress; }
    QString rebaseOnto() const { return QString::fromStdString(m_rebase.ontoRef); }
    int     rebaseStep() const { return m_rebase.current; }
    int     rebaseTotal() const { return m_rebase.total; }
    QString rebaseStepSummary() const { return QString::fromStdString(m_rebase.stepSummary); }
    int     rebaseConflictedCount() const { return int(m_rebase.conflictedPaths.size()); }
    bool    rebaseHasSubmoduleConflicts() const { return !m_rebase.conflictedSubmodules.empty(); }
    bool    rebaseInteractive() const { return m_rebase.interactive; }
    QString rebasePauseReason() const
    {
        switch (m_rebase.pause)
        {
            case gittide::RebasePause::Conflict: return "conflict";
            case gittide::RebasePause::Message:  return "message";
            default:                             return "none";
        }
    }
    QString rebaseMessagePrefill() const { return QString::fromStdString(m_rebase.messagePrefill); }

    Q_INVOKABLE void open(const QString& path);
    /// Reset to the no-repo state: clears the file/diff/branch/history models and
    /// all selection/sync state so the working pane falls back to the empty state.
    /// Used when switching to a project that has no repository to show.
    Q_INVOKABLE void close();
    Q_INVOKABLE void selectFile(const QString& path);
    Q_INVOKABLE void selectFileAtRow(int row);
    Q_INVOKABLE void selectCommitAtRow(int row);
    Q_INVOKABLE void selectGraphCommitAtRow(int row);
    Q_INVOKABLE void selectCommitFileAtRow(int row);
    Q_INVOKABLE void setFileChecked(int row, bool checked);
    Q_INVOKABLE void setAllFilesChecked(bool checked);
    Q_INVOKABLE void setLineChecked(int row, bool checked);
    Q_INVOKABLE void setAllLinesChecked(bool checked);
    Q_INVOKABLE void setBlockChecked(int row, bool checked);
    Q_INVOKABLE void commit(const QString& summary, const QString& description);

    Q_INVOKABLE void selectCommit(const QString& oid);
    Q_INVOKABLE void selectCommitFile(const QString& path);
    // Single entry point from HistoryPane: rows is the selected row-index set.
    Q_INVOKABLE void selectCommitRows(const QVariantList& rows);
    Q_INVOKABLE void checkoutCommit(const QString& oid);
    Q_INVOKABLE void rewordHead(const QString& message);
    Q_INVOKABLE void undoLastCommit();
    Q_INVOKABLE void requestCommitMessage(const QString& oid);

    Q_INVOKABLE void switchBranch(const QString& name);
    Q_INVOKABLE void checkoutRemoteBranch(const QString& remoteShorthand);
    Q_INVOKABLE void createBranch(const QString& name, const QString& fromOid, bool checkout);
    Q_INVOKABLE void deleteBranch(const QString& name, bool force);
    Q_INVOKABLE void renameBranch(const QString& oldName, const QString& newName);
    Q_INVOKABLE void refreshHistory();
    Q_INVOKABLE void refreshGraph();
    /// Re-sync the whole active repo from disk (status + branches + history +
    /// sync). Called on window focus-in (D35) to catch changes the directory
    /// watcher can miss (in-place edits of an existing file while backgrounded).
    Q_INVOKABLE void resync();

    Q_INVOKABLE void fetch();
    Q_INVOKABLE void pull();
    Q_INVOKABLE void push();
    Q_INVOKABLE void publishBranch();
    Q_INVOKABLE void submitCredentials(const QString& username, const QString& token);

    /// Wire the process-wide credential manager so sync ops resolve keychain-backed
    /// credentials for the active remote (and the auth-dialog fallback persists them).
    void setCredentialManager(CredentialManager* cm) { m_credentials = cm; }
    Q_INVOKABLE void applyPullDefault(bool rebase);

    /// Accept one side of a single conflict region and write the resolved file.
    /// @p region is the 0-based region index from DiffLinesModel::ConflictRegionRole.
    /// @p which selects the resolution: 0 = ours (current), 1 = theirs (incoming),
    /// 2 = both (ours then theirs). Writes the result via the controller (FS stays
    /// in the controller; the VM never touches the filesystem directly), then
    /// re-selects the file so the diff view and MergeState refresh (D30).
    Q_INVOKABLE void acceptConflict(int region, int which);

    /// Begin merging @p name into the current branch. Stores the name for
    /// retryMergeDeinitSubmodules(). On conflict the VM will emit mergeStateChanged.
    Q_INVOKABLE void startMerge(const QString& name);
    /// Commit the in-progress merge with the supplied @p message.
    Q_INVOKABLE void commitMerge(const QString& message);
    /// Abort the in-progress merge and restore the working tree to pre-merge state.
    Q_INVOKABLE void abortMerge();
    /// Deinit conflicting submodules then retry the merge started by startMerge().
    Q_INVOKABLE void retryMergeDeinitSubmodules();

    /// Begin rebasing the current branch onto @p ref.
    Q_INVOKABLE void startRebase(const QString& ref);
    Q_INVOKABLE void startInteractiveRebase(QString base, QStringList actions, QStringList oids);
    /// Squash the selected history rows: map them to oids and ask the controller to
    /// run a contiguous squash — starts the rebase directly and jumps to the
    /// combined-message edit (no todo editor).
    Q_INVOKABLE void requestSquashTodo(const QVariantList& rows);
    /// Ask the controller for the editable plan fromOid..HEAD; reply on rebaseTodoReady.
    Q_INVOKABLE void requestRebaseTodo(QString fromOid);
    /// Reorder the commit at history row @p fromRow next to @p toRow within the
    /// reorderable run (both must be < reorderableRunLength). @p band selects the
    /// side of the target the dragged commit lands on: "above" inserts one slot
    /// newer than the target, "below" (the default) one slot older. Replays the run
    /// in the new order via an interactive rebase (all picks). No-op if out of range.
    Q_INVOKABLE void reorderCommits(int fromRow, int toRow, const QString& band = QStringLiteral("below"));
    /// Squash the commit at @p fromRow into the commit at @p toRow (both
    /// newest-first indices into the reorderable run). The dragged commit folds
    /// into the target; the combined commit keeps the target's slot. Drives the
    /// interactive engine, which pauses on the combined message (RewordDialog).
    /// No-op unless both rows are in [0, reorderableRunLength) and differ.
    Q_INVOKABLE void squashCommitInto(int fromRow, int toRow);
    /// Continue the in-progress rebase after resolving conflicts or with a commit message.
    Q_INVOKABLE void continueRebase(const QString& message = QString());
    /// Skip the current conflicting commit and advance to the next step.
    Q_INVOKABLE void skipRebase();
    /// Abort the in-progress rebase and restore the pre-rebase state.
    Q_INVOKABLE void abortRebase();

    Q_INVOKABLE void discardFile(const QString& path);
    Q_INVOKABLE void discardAll();
    /// Stash all working-tree changes (no message prompt).
    Q_INVOKABLE void stashChanges();
    /// Pop the most-recent stash back onto the working tree.
    Q_INVOKABLE void popStash();
    /// Preview the stash at @p row in the commit-diff view.
    Q_INVOKABLE void previewStash(int row);
    /// Exit stash preview mode and clear the commit-diff view.
    Q_INVOKABLE void exitStashPreview();
    /// Apply the stash at @p row (keep it in the stack).
    Q_INVOKABLE void applyStash(int row);
    /// Pop (apply + drop) the stash at @p row.
    Q_INVOKABLE void popStashAt(int row);
    /// Drop the stash at @p row without applying it.
    Q_INVOKABLE void dropStash(int row);
    /// Drop all stash entries.
    Q_INVOKABLE void clearStashes();
    Q_INVOKABLE void openInEditor(const QString& path);
    Q_INVOKABLE void revealInFileManager(const QString& path);
    /// Open the repository root folder in the OS-native file manager.
    Q_INVOKABLE void openRepoFolder();
    Q_INVOKABLE void copyToClipboard(const QString& text);

signals:
    void changed();
    /// Emitted whenever the working-tree status is rebuilt (every onStatus()
    /// refresh), so the dirty property re-evaluates on dirty↔clean transitions.
    void dirtyChanged();
    /// Emitted when the stash count changes; NOTIFY for the stashAvailable property.
    void stashCountChanged();
    void branchChanged();
    void activeFileChanged();
    void checkedChanged();
    void committedOk();
    void operationFailed(const QString& message);
    void branchDeleteUnmerged(const QString& name);
    void selectedCommitChanged();
    void commitDetailChanged();
    void activeCommitFileChanged();
    void historyDetailChanged();
    void reorderableRunChanged();
    void commitMessageReady(const QString& oid, const QString& message);
    void syncStatusChanged();
    void syncingChanged();
    void syncProgressChanged();
    void pullRebaseChanged();
    void authRequired();
    /// Emitted whenever the MergeState changes (merge started, conflict resolved,
    /// merge committed or aborted). All merge-related Q_PROPERTYs use this NOTIFY.
    void mergeStateChanged();
    /// Emitted whenever the RebaseState changes (rebase started, conflict resolved,
    /// step applied, rebase finished or aborted). All rebase-related Q_PROPERTYs use this NOTIFY.
    void rebaseStateChanged();
    /// Emitted once when the rebase newly enters a Message pause (squash/reword
    /// step). Drives WorkingPane to auto-open the commit-message dialog.
    void rebaseMessagePauseEntered();
    /// Forwarded from the controller: seed plan for the interactive editor.
    /// `entries` is a list of QVariantMap{oid, summary}, oldest first; `base` is the detach commit oid.
    void rebaseTodoReady(QString base, QVariantList entries);

    /// Emitted when the git-dir watcher fires a full refresh (gitDirRefreshed
    /// from RepoController), indicating that repository structure may have changed
    /// on disk (e.g. submodule init/deinit via an external terminal).
    void repoStructureChanged();

    /// Emitted when stashPreviewActive or stashPreviewLabel changes.
    void stashPreviewChanged();

private:
    // Sync helpers: resolve credentials (session override → keychain-backed
    // credentialsForRemote → agent default) then run the op through the controller.
    QCoro::Task<gittide::Credentials> resolveCredentials();
    QCoro::Task<void>                 runFetch();
    QCoro::Task<void>                 runPull();
    QCoro::Task<void>                 runPush(bool setUpstream);

    struct FileSel
    {
        ChangedFilesModel::Check        state = ChangedFilesModel::Checked;
        std::map<int, std::vector<int>> checkedLinesByHunk; // only meaningful when state == Partial
    };

    void onStatus(const std::vector<gittide::FileStatus>& files);
    void onStashCount(int count);
    void onStashList(const std::vector<gittide::StashEntry>& entries);
    void onDiff(const QString& path, const gittide::DiffResult& result);
    void onHead(const gittide::HeadState& head);
    void onBranches(const std::vector<gittide::BranchInfo>& branches);
    void onHistory(const gittide::GraphLayout& layout);
    void onGraph(const gittide::GraphLayout& layout);
    void onRefTips(const QHash<QString, QStringList>& oidToLabels);
    void onLocalOnly(const QSet<QString>& oids);
    void applyHistoryIfReady();
    void updateReorderableRun();
    void onLineToggled(int hunkIndex, int lineIndex, bool checked);
    void recomputeActiveFileState();
    void onCommitFiles(const QString& oid, const std::vector<gittide::FileStatus>& files);
    void onCommitDetail(const QString& oid, const gittide::CommitDetail& detail);
    void clearCommitDetail();
    void onCommitDiff(const QString& oid, const QString& path, const gittide::DiffResult& result);
    // Range-mode routing helpers (called by selectCommitRows):
    void applyRange(const QString& oldOid, const QString& newOid, int count);
    void applyRangeHint();
    // Range-mode async handlers:
    void onRangeFiles(const QString& oldOid, const QString& newOid,
                      const std::vector<gittide::FileStatus>& files);
    void onRangeDiff(const QString& oldOid, const QString& newOid, const QString& path,
                     const gittide::DiffResult& result);

    // Applies a whole-file check to one row without emitting checkedChanged;
    // returns true if the row was valid. Callers coalesce the emit.
    bool applyFileChecked(int row, bool checked);

    RepoController*    m_controller = nullptr;
    ChangedFilesModel* m_files      = nullptr;
    DiffLinesModel*    m_diff       = nullptr;
    BranchListModel*   m_branches   = nullptr;
    HistoryListModel*  m_history    = nullptr;
    HistoryListModel*  m_graph      = nullptr;
    ChangedFilesModel* m_commitFiles = nullptr;
    DiffLinesModel*    m_commitDiff  = nullptr;
    StashListModel*    m_stashes     = nullptr;

    bool                       m_open = false;
    int                        m_stashCount = 0;
    bool                       m_stashPreviewActive = false;
    QString                    m_stashPreviewLabel;
    int                        m_stashPreviewIndex  = -1;
    gittide::MergeState        m_merge;
    QString                    m_mergeStartName;
    gittide::RebaseState       m_rebase;
    gittide::SyncStatus        m_sync;
    bool                       m_syncing    = false;
    int                        m_syncReceived = 0;
    int                        m_syncTotal    = 0;
    bool                       m_pullRebase = false;
    gittide::Credentials       m_sessionCred;
    CredentialManager*         m_credentials = nullptr; // process-wide; not owned
    QString                    m_remoteUrl;             // cached for auth-dialog persistence
    enum class PendingOp { None, Fetch, Pull, Push, Publish } m_pendingOp = PendingOp::None;
    QString                    m_selectedCommit;
    QString                    m_detailSummary, m_detailBody, m_detailAuthor, m_detailAuthorEmail, m_detailDate;
    int                        m_detailFilesChanged = 0, m_detailAdditions = 0, m_detailDeletions = 0;
    QString                    m_activeCommitFile;
    QString                    m_rangeOld;     ///< non-empty only in range mode
    QString                    m_rangeNew;
    QString                    m_detailHeader;
    QString                    m_detailHint;
    // History population is reconciled from two async signals (historyReady and
    // headChanged) that can arrive in either order. We cache both and apply once
    // both have landed for the current open(), so IsHeadRole is always correct
    // and an unborn/empty repo still resets the model. The flags distinguish
    // "not yet received" from "received but empty" (an unborn HEAD has an empty oid).
    QString                    m_headOid;
    gittide::GraphLayout       m_lastLayout;
    int                        m_reorderableRunLength = 0;
    bool                       m_headArrived    = false;
    bool                       m_historyArrived = false;
    QString                    m_branch;
    QString                    m_headBranch;  ///< Real ref name; empty when detached or unborn.
    QString                    m_activeFile;
    std::map<QString, FileSel> m_sel;
};

} // namespace gittide::ui
