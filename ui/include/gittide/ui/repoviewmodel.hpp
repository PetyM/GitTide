#pragma once
#include <map>
#include <vector>

#include <QObject>
#include <QString>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/ui/changedfilesmodel.hpp"
#include "gittide/ui/difflinesmodel.hpp"

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

public:
    explicit RepoViewModel(QObject* parent = nullptr);

    bool repoOpen() const;
    QString currentBranch() const;
    QString activeFile() const;
    int checkedCount() const;
    ChangedFilesModel* changedFiles() const;
    DiffLinesModel* diffLines() const;

    Q_INVOKABLE void open(const QString& path);
    Q_INVOKABLE void selectFile(const QString& path);
    Q_INVOKABLE void setFileChecked(int row, bool checked);
    Q_INVOKABLE void setAllFilesChecked(bool checked);
    Q_INVOKABLE void setLineChecked(int row, bool checked);
    Q_INVOKABLE void setAllLinesChecked(bool checked);
    Q_INVOKABLE void commit(const QString& summary, const QString& description);

signals:
    void changed();
    void branchChanged();
    void activeFileChanged();
    void checkedChanged();
    void committedOk();
    void operationFailed(const QString& message);

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
    void onLineToggled(int hunkIndex, int lineIndex, bool checked);
    void recomputeActiveFileState();

    // Applies a whole-file check to one row without emitting checkedChanged;
    // returns true if the row was valid. Callers coalesce the emit.
    bool applyFileChecked(int row, bool checked);

    RepoController*    m_controller = nullptr;
    ChangedFilesModel* m_files      = nullptr;
    DiffLinesModel*    m_diff       = nullptr;

    bool                       m_open = false;
    QString                    m_branch;
    QString                    m_activeFile;
    std::map<QString, FileSel> m_sel;
};

} // namespace gittide::ui
