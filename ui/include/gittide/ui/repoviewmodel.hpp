#pragma once
#include <map>
#include <vector>

#include <QObject>
#include <QString>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"
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

public:
    explicit RepoViewModel(QObject* parent = nullptr);

    bool repoOpen() const;
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

    Q_INVOKABLE void open(const QString& path);
    Q_INVOKABLE void selectFile(const QString& path);
    Q_INVOKABLE void setFileChecked(int row, bool checked);
    Q_INVOKABLE void setAllFilesChecked(bool checked);
    Q_INVOKABLE void setLineChecked(int row, bool checked);
    Q_INVOKABLE void setAllLinesChecked(bool checked);
    Q_INVOKABLE void commit(const QString& summary, const QString& description);

    Q_INVOKABLE void selectCommit(const QString& oid);
    Q_INVOKABLE void selectCommitFile(const QString& path);
    Q_INVOKABLE void checkoutCommit(const QString& oid);

    Q_INVOKABLE void switchBranch(const QString& name);
    Q_INVOKABLE void createBranch(const QString& name, const QString& fromOid, bool checkout);
    Q_INVOKABLE void deleteBranch(const QString& name, bool force);
    Q_INVOKABLE void renameBranch(const QString& oldName, const QString& newName);
    Q_INVOKABLE void refreshHistory();

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
    QString                    m_activeFile;
    std::map<QString, FileSel> m_sel;
};

} // namespace gittide::ui
