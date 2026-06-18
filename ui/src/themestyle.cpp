#include "gittide/ui/themestyle.hpp"

namespace gittide::ui {

QPalette buildPalette(const Theme& t)
{
    QPalette p;

    // Active / Inactive groups — full token mapping.
    for (auto group : {QPalette::Active, QPalette::Inactive})
    {
        p.setColor(group, QPalette::Window,          QColor(t.surfaceBase));
        p.setColor(group, QPalette::WindowText,      QColor(t.textPrimary));
        p.setColor(group, QPalette::Text,            QColor(t.textPrimary));
        p.setColor(group, QPalette::ButtonText,      QColor(t.textPrimary));
        p.setColor(group, QPalette::Base,            QColor(t.surfaceRaised));
        p.setColor(group, QPalette::AlternateBase,   QColor(t.surfaceOverlay));
        p.setColor(group, QPalette::Button,          QColor(t.surfaceRaised));
        p.setColor(group, QPalette::ToolTipBase,     QColor(t.surfaceOverlay));
        p.setColor(group, QPalette::ToolTipText,     QColor(t.textPrimary));
        p.setColor(group, QPalette::Highlight,       QColor(t.accent));
        p.setColor(group, QPalette::HighlightedText, QColor(t.surfaceBase));
        p.setColor(group, QPalette::PlaceholderText, QColor(t.textMuted));
        p.setColor(group, QPalette::Link,            QColor(t.accent));
    }

    // Disabled group — muted text.
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(t.textMuted));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(t.textMuted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(t.textMuted));

    return p;
}

QString buildAccentStyleSheet(const Theme& t)
{
    // QSS for cues that a QPalette cannot express:
    //   - QTabBar selected-tab underline
    //   - Selection left-border on named list widgets
    //   - Focus ring
    //   - Diff gutter added/deleted row background
    return QStringLiteral(
               "QTabBar::tab:selected {"
               "    border-bottom: 2px solid %1;"
               "}"
               "#changedFilesList::item:selected {"
               "    border-left: 2px solid %1;"
               "}"
               "#repoList::item:selected {"
               "    border-left: 2px solid %1;"
               "}"
               "*:focus {"
               "    outline: 2px solid %1;"
               "}"
               "QFrame[diffRole=\"added\"] {"
               "    background: %2;"
               "}"
               "QFrame[diffRole=\"deleted\"] {"
               "    background: %3;"
               "}")
        .arg(t.accent,       // %1 — accent
             t.stateAdded,   // %2 — diff gutter added
             t.stateDeleted  // %3 — diff gutter deleted
        );
}

} // namespace gittide::ui
