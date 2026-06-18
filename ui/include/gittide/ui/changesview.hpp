#pragma once
#include <QWidget>
#include <map>
#include <vector>

#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/ui/changedfileslist.hpp"

class QPlainTextEdit;
class QPushButton;

namespace gittide::ui {

// Changes tab body (post staging-area removal, D23). Hosts a ChangedFilesList in
// Editable mode and pins a commit message box + Commit button at the bottom. The
// diff is NOT embedded here — it is a shared panel owned by MainWindow; this view
// only owns the per-file commit-selection model and exposes the file list so
// MainWindow can wire fileSelected → the shared DiffView.
//
// A commit is built from the CHECKED set: each non-Unchecked file contributes a
// StageSelection (whole file when Checked, per-hunk line selections when Partial).
class ChangesView : public QWidget
{
    Q_OBJECT
public:
    explicit ChangesView(QWidget* parent = nullptr);

    // Populate from a status vector. Resets the selection model: every file Checked.
    void setStatus(const std::vector<gittide::FileStatus>& files);

    // The hosted file list (MainWindow wires its fileSelected → shared diff).
    ChangedFilesList* filesList() const
    {
        return m_files;
    }

    QString commitMessage() const;

    // Apply a user line toggle coming back from the shared DiffView. Moves the
    // file to Partial and updates checkedLinesByHunk[hunkIndex] (adds or removes
    // lineIndex). Collapses to Checked if every tracked line ends up checked, to
    // Unchecked if none remain checked. Pushes the resulting tri-state to the list
    // via setRowCheck. MainWindow forwards DiffView::lineCheckToggled here.
    void applyLineToggle(const QString& path, int hunkIndex, int lineIndex, bool checked);

    // The current per-file selection for a path, so MainWindow can populate the
    // shared DiffView when that file is selected. wholeChecked is true unless the
    // file is Partial; checkedLines carries the per-hunk checked line indices when
    // Partial (empty otherwise).
    void selectionFor(const QString& path, bool& wholeChecked,
                      std::map<int, std::vector<int>>& checkedLines) const;

signals:
    void commitRequested(const gittide::CommitRequest& req,
                         std::vector<gittide::StageSelection> selections);
    void discardRequested(const gittide::StageSelection& sel);

private:
    // Per-file selection state (default Checked). Partial stores checked line
    // indices per hunk; whole-file Checked/Unchecked carry no line map.
    struct FileSel
    {
        ChangedFilesList::Check state = ChangedFilesList::Check::Checked;
        std::map<int, std::vector<int>> checkedLinesByHunk; // only when Partial
    };

    void updateCommitEnabled();

    ChangedFilesList* m_files;
    QPlainTextEdit*   m_message;
    QPushButton*      m_commitButton;
    std::map<QString, FileSel> m_sel;
};

} // namespace gittide::ui
