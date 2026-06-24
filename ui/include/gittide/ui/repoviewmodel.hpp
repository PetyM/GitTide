#pragma once
#include <map>
#include <vector>

#include <QObject>
#include <QString>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"
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
    Q_INVOKABLE void checkoutCommit(const QString& oid);
    Q_INVOKABLE void rewordHead(const QString& message);
    Q_INVOKABLE void requestCommitMessage(const QString& oid);

    Q_INVOKABLE void switchBranch(const QString& name);
    Q_INVOKABLE void checkoutRemoteBranch(const QString& remoteShorthand);
    Q_INVOKABLE void createBranch(const QString& name, const QString& fromOid, bool checkout);
    Q_INVOKABLE void deleteBranch(const QString& name, bool force);
    Q_INVOKABLE void renameBranch(const QString& oldName, const QString& newName);
    Q_INVOKABLE void refreshHistory();

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
    void commitMessageReady(const QString& oid, const QString& message);
    void syncStatusChanged();
    void syncingChanged();
    void syncProgressChanged();
    void pullRebaseChanged();
    void authRequired();
    /// Emitted whenever the MergeState changes (merge started, conflict resolved,
    /// merge committed or aborted). All merge-related Q_PROPERTYs use this NOTIFY.
    void mergeStateChanged();

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
    void onLineToggled(int hunkIndex, int lineIndex, bool checked);
    void recomputeActiveFileState();
    void onCommitFiles(const QString& oid, const std::vector<gittide::FileStatus>& files);
    void onCommitDiff(const QString& oid, const QString& path, const gittide::DiffResult& result);

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
    gittide::SyncStatus        m_sync;
    bool                       m_syncing    = false;
    int                        m_syncReceived = 0;
    int                        m_syncTotal    = 0;
    bool                       m_pullRebase = false;
    gittide::Credentials       m_sessionCred;
    enum class PendingOp { None, Fetch, Pull, Push, Publish } m_pendingOp = PendingOp::None;
    QString                    m_selectedCommit;
    QString                    m_activeCommitFile;
    // History population is reconciled from two async signals (historyReady and
    // headChanged) that can arrive in either order. We cache both and apply once
    // both have landed for the current open(), so IsHeadRole is always correct
    // and an unborn/empty repo still resets the model. The flags distinguish
    // "not yet received" from "received but empty" (an unborn HEAD has an empty oid).
    QString                    m_headOid;
    gittide::GraphLayout       m_lastLayout;
    bool                       m_headArrived    = false;
    bool                       m_historyArrived = false;
    QString                    m_branch;
    QString                    m_headBranch;  ///< Real ref name; empty when detached or unborn.
    QString                    m_activeFile;
    std::map<QString, FileSel> m_sel;
};

} // namespace gittide::ui
