#pragma once
#include <QWidget>
#include <vector>

#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"

class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace gitgui::ui {

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

    void setStatus(const std::vector<gitgui::FileStatus>& files);
    void setDiff(const gitgui::DiffResult& result, const std::filesystem::path& file);
    QString commitMessage() const;
    DiffView* diffView() const { return diff_; }

signals:
    void fileSelected(const QString& path, gitgui::DiffTarget target);
    void commitRequested(const gitgui::CommitRequest& req);
    void stageRequested(const gitgui::StageSelection& sel);
    void unstageRequested(const gitgui::StageSelection& sel);
    void discardRequested(const gitgui::StageSelection& sel);

private:
    void updateCommitEnabled();

    QListWidget* staged_;
    QListWidget* unstaged_;
    DiffView* diff_;
    QPlainTextEdit* message_;
    QPushButton* commitButton_;
};

}  // namespace gitgui::ui
