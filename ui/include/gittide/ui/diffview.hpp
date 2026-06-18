#pragma once
#include <QWidget>
#include <filesystem>
#include <map>
#include <vector>

#include "gittide/diff.hpp"

class QListWidget;

namespace gittide::ui {

// Renders a DiffResult as one row per diff line.
//
// Editable mode: added/removed line rows carry user-checkable checkboxes.
// The initial check state is driven by wholeChecked and checkedLines (see
// setDiff).  Toggling a checkbox emits lineCheckToggled.  A context menu on
// the list offers Discard, emitting discardRequested with a StageSelection
// covering the checked line(s) in the right-clicked hunk (or the whole file
// when nothing specific is selected).
//
// ReadOnly mode: diff rendered without checkboxes (history, blame, etc.).
class DiffView : public QWidget
{
    Q_OBJECT
public:
    enum class Mode
    {
        Editable,
        ReadOnly,
    };

    explicit DiffView(QWidget* parent = nullptr);

    void setMode(Mode mode);

    // Populate the view.
    //   file         — repo-relative path shown in signals and selections.
    //   wholeChecked — when checkedLines is empty, check all added/removed
    //                  lines iff true.
    //   checkedLines — hunkIndex → line indices that are checked.  When
    //                  non-empty it is authoritative (overrides wholeChecked
    //                  on a per-hunk basis).
    void setDiff(const gittide::DiffResult& result,
                 const std::filesystem::path& file,
                 bool wholeChecked,
                 const std::map<int, std::vector<int>>& checkedLines);

    void clear();

signals:
    // Emitted only in Editable mode when the USER toggles a checkbox.
    void lineCheckToggled(const QString& path, int hunkIndex, int lineIndex, bool checked);

    // Emitted via context-menu Discard action.
    void discardRequested(const gittide::StageSelection& sel);

private:
    QListWidget* m_lines;
    std::filesystem::path m_file;
    Mode m_mode{Mode::Editable};
    bool m_filling{false}; // re-entrancy guard for programmatic fills
};

} // namespace gittide::ui
