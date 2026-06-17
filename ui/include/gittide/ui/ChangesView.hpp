#pragma once
#include <QWidget>
#include <vector>

#include "gittide/Diff.hpp"
#include "gittide/FileStatus.hpp"

class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace gittide::ui {

class DiffView;

// Changes tab body: staged + unstaged file lists (left), DiffView (right),
// commit message + button (bottom). A file is listed under "staged" if it has
// any Index* flag and under "unstaged" if it has any Wt* flag (it may appear in
// both). Selecting a file emits fileSelected(path, target); target is IndexVsHead
// for the staged list, WorktreeVsIndex for the unstaged list.
class ChangesView : public QWidget {
    Q_OBJECT
public:
    explicit ChangesView(QWidget* parent = nullptr);

    void setStatus(const std::vector<gittide::FileStatus>& files);
    void setDiff(const gittide::DiffResult& result, const std::filesystem::path& file);
    QString commitMessage() const;
    DiffView* diffView() const { return diff_; }

signals:
    void fileSelected(const QString& path, gittide::DiffTarget target);
    void commitRequested(const gittide::CommitRequest& req);
    void stageRequested(const gittide::StageSelection& sel);
    void unstageRequested(const gittide::StageSelection& sel);
    void discardRequested(const gittide::StageSelection& sel);

private:
    void updateCommitEnabled();

    QListWidget* staged_;
    QListWidget* unstaged_;
    DiffView* diff_;
    QPlainTextEdit* message_;
    QPushButton* commitButton_;
};

}  // namespace gittide::ui
