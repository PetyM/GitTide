#pragma once
#include <QWidget>
#include <vector>

#include "gittide/filestatus.hpp"

class QListWidget;
class QListWidgetItem;

namespace gittide::ui {

// A single list of changed files. Each row shows:
//   - [Editable only] tri-state checkbox (Unchecked / Checked / Partial)
//   - file path
//   - trailing status letter (A/M/D/U) coloured from the state* theme tokens
//
// Two modes:
//   Editable  — checkboxes visible, rows start Checked; used by the Changes tab.
//   ReadOnly  — no checkboxes; used to show a commit's files under History.
//
// Re-entrancy guard: setRowCheck() does NOT emit fileCheckToggled; only a real
// user click emits the signal.
class ChangedFilesList : public QWidget
{
    Q_OBJECT
public:
    enum class Mode { Editable, ReadOnly };
    enum class Check { Unchecked, Checked, Partial };

    explicit ChangedFilesList(QWidget* parent = nullptr);

    void setMode(Mode mode);

    // Populate the list from a status vector.
    // In Editable mode every row starts Checked.
    void setFiles(const std::vector<gittide::FileStatus>& files);

    // Reflect a file's tri-state programmatically (does NOT emit fileCheckToggled).
    void setRowCheck(const QString& path, Check check);

    // Returns paths of fully-checked rows (Check::Checked only).
    std::vector<QString> checkedPaths() const;

signals:
    // Emitted when the current (selected) item changes.
    void fileSelected(const QString& path, gittide::StatusFlag flags);

    // Emitted only on a real user click on the checkbox — never by setRowCheck.
    void fileCheckToggled(const QString& path, bool checked);

    // Emitted from the context menu ("Discard changes…") in Editable mode.
    void discardRequested(const QString& path);

private:
    void onItemChanged(QListWidgetItem* item);
    void onCurrentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void showContextMenu(const QPoint& pos);

    QListWidget* m_list;
    Mode         m_mode    = Mode::Editable;
    bool         m_updating = false; // re-entrancy guard for setRowCheck
};

} // namespace gittide::ui
