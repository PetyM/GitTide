#pragma once
#include <map>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariantList>

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

namespace gittide::ui {

class RepoController;

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
    Q_PROPERTY(gittide::ui::ChangedFilesModel* commitFiles READ commitFiles CONSTANT)
    Q_PROPERTY(gittide::ui::DiffLinesModel* commitDiff READ commitDiff CONSTANT)
    Q_PROPERTY(QString selectedCommit READ selectedCommit NOTIFY selectedCommitChanged)
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
    QString repoPath() const;
    QString currentBranch() const;
    QString activeFile() const;
    int checkedCount() const;
    ChangedFilesModel* changedFiles() const;
    DiffLinesModel* diffLines() const;
    BranchListModel*    branches() const;
    HistoryListModel*   history() const;
    ChangedFilesModel* commitFiles() const;
    DiffLinesModel* commitDiff() const;
    QString selectedCommit() const;
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
    /// Re-sync the whole active repo from disk (status + branches + history +
    /// sync). Called on window focus-in (D35) to catch changes the directory
    /// watcher can miss (in-place edits of an existing file while backgrounded).
    Q_INVOKABLE void resync();

    Q_INVOKABLE void fetch();
    Q_INVOKABLE void pull();
    Q_INVOKABLE void push();
    Q_INVOKABLE void publishBranch();
    Q_INVOKABLE void submitCredentials(const QString& username, const QString& token);
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
    /// build a contiguous squash plan; reply on rebaseTodoReady (opens the editor).
    Q_INVOKABLE void requestSquashTodo(const QVariantList& rows);
    /// Ask the controller for the editable plan fromOid..HEAD; reply on rebaseTodoReady.
    Q_INVOKABLE void requestRebaseTodo(QString fromOid);
    /// Reorder the commit at history row @p fromRow to @p toRow within the
    /// reorderable run (both must be < reorderableRunLength). Replays the run in the
    /// new order via an interactive rebase (all picks). No-op if out of range.
    Q_INVOKABLE void reorderCommits(int fromRow, int toRow);
    /// Continue the in-progress rebase after resolving conflicts or with a commit message.
    Q_INVOKABLE void continueRebase(const QString& message = QString());
    /// Skip the current conflicting commit and advance to the next step.
    Q_INVOKABLE void skipRebase();
    /// Abort the in-progress rebase and restore the pre-rebase state.
    Q_INVOKABLE void abortRebase();

    Q_INVOKABLE void discardFile(const QString& path);
    Q_INVOKABLE void openInEditor(const QString& path);
    Q_INVOKABLE void revealInFileManager(const QString& path);
    Q_INVOKABLE void copyToClipboard(const QString& text);

signals:
    void changed();
    void branchChanged();
    void activeFileChanged();
    void checkedChanged();
    void committedOk();
    void operationFailed(const QString& message);
    void branchDeleteUnmerged(const QString& name);
    void selectedCommitChanged();
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
    /// Forwarded from the controller: seed plan for the interactive editor.
    /// `entries` is a list of QVariantMap{oid, summary}, oldest first; `base` is the detach commit oid.
    void rebaseTodoReady(QString base, QVariantList entries);

private:
    struct FileSel
    {
        ChangedFilesModel::Check        state = ChangedFilesModel::Checked;
        std::map<int, std::vector<int>> checkedLinesByHunk; // only meaningful when state == Partial
    };

    void onStatus(const std::vector<gittide::FileStatus>& files);
    void onDiff(const QString& path, const gittide::DiffResult& result);
    void onHead(const gittide::HeadState& head);
    void onBranches(const std::vector<gittide::BranchInfo>& branches);
    void onHistory(const gittide::GraphLayout& layout);
    void applyHistoryIfReady();
    void updateReorderableRun();
    void onLineToggled(int hunkIndex, int lineIndex, bool checked);
    void recomputeActiveFileState();
    void onCommitFiles(const QString& oid, const std::vector<gittide::FileStatus>& files);
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
    ChangedFilesModel* m_commitFiles = nullptr;
    DiffLinesModel*    m_commitDiff  = nullptr;

    bool                       m_open = false;
    gittide::MergeState        m_merge;
    QString                    m_mergeStartName;
    gittide::RebaseState       m_rebase;
    gittide::SyncStatus        m_sync;
    bool                       m_syncing    = false;
    int                        m_syncReceived = 0;
    int                        m_syncTotal    = 0;
    bool                       m_pullRebase = false;
    gittide::Credentials       m_sessionCred;
    enum class PendingOp { None, Fetch, Pull, Push, Publish } m_pendingOp = PendingOp::None;
    QString                    m_selectedCommit;
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
