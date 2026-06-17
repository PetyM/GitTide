#pragma once
#include <QWidget>
#include <filesystem>
#include <optional>

#include "gitgui/Diff.hpp"

class QListWidget;

namespace gitgui::ui {

// Renders a DiffResult as one row per diff line. The user selects lines and
// triggers stage/unstage/discard; the view builds a StageSelection covering the
// selected lines (constrained to a single hunk — the hunk of the first selected
// line) and emits it. Context lines are shown but ignored when building selection.
class DiffView : public QWidget {
    Q_OBJECT
public:
    explicit DiffView(QWidget* parent = nullptr);

    void setDiff(const gitgui::DiffResult& result, const std::filesystem::path& file);
    void clear();

    // Build a selection from the currently selected non-context lines.
    // nullopt when nothing stageable is selected.
    std::optional<gitgui::StageSelection> currentSelection() const;

public slots:
    void requestStage();
    void requestUnstage();
    void requestDiscard();

signals:
    void stageRequested(const gitgui::StageSelection& sel);
    void unstageRequested(const gitgui::StageSelection& sel);
    void discardRequested(const gitgui::StageSelection& sel);

private:
    QListWidget* lines_;
    std::filesystem::path file_;
};

}  // namespace gitgui::ui
