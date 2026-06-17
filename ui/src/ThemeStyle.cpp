#include "gittide/ui/ThemeStyle.hpp"

namespace gittide::ui {

QString buildStyleSheet(const Theme& t) {
    return QStringLiteral(R"(
QWidget {
    background: %1;
    color: %5;
    font-family: system-ui, -apple-system, "Segoe UI", "Noto Sans", sans-serif;
    font-size: 13px;
}
QDockWidget, QFrame#emptyStateCard, QTabWidget::pane, QDialog {
    background: %2;
    border: 1px solid %4;
    border-radius: 10px;
}
QLabel { background: transparent; }
QLabel[role="headline"] { font-size: 22px; font-weight: 700; color: %5; }
QLabel[role="subtext"]  { font-size: 13px; color: %6; }

QPushButton, QToolButton {
    background: %2;
    color: %5;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 8px 16px;
    font-weight: 600;
}
QPushButton:hover, QToolButton:hover { border-color: %8; }

QPushButton#createProjectCta, QPushButton#addExistingCta {
    background: %8;
    color: %1;
    border: none;
}
QPushButton#createProjectCta:hover, QPushButton#addExistingCta:hover { background: %9; }

QComboBox#projectSwitcher {
    background: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 12px;
}

QTreeView#repoList {
    background: %2; border: 1px solid %4; border-radius: 10px;
}
QTreeView#repoList::item { padding: 4px 8px; }
QTreeView#repoList::item:selected {
    background: %3; border-left: 2px solid %8; color: %5;
}

QTabBar::tab { background: transparent; color: %6; padding: 8px 16px; }
QTabBar::tab:selected { color: %5; border-bottom: 2px solid %8; }

QProgressBar { border: 1px solid %4; border-radius: 6px; text-align: center; }
QProgressBar::chunk { background: %8; border-radius: 6px; }
)")
        .arg(t.surfaceBase,    // %1
             t.surfaceRaised,  // %2
             t.surfaceOverlay, // %3
             t.border,         // %4
             t.textPrimary,    // %5
             t.textSecondary,  // %6
             t.textMuted,      // %7
             t.accent,         // %8
             t.accentHover);   // %9
}

}  // namespace gittide::ui
