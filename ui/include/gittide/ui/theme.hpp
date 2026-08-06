#pragma once
#include <QString>

namespace gittide::ui {

// A resolved set of design tokens (one theme). Values come from
// docs/spec/design/design.md (§ Surface / Accent token tables).
struct Theme
{
    bool dark;
    QString surfaceBase, surfaceRaised, surfaceOverlay, border;
    QString textPrimary, textSecondary, textMuted;
    QString accent, accentHover, head;
    QString onAccent; // Label colour on a filled accent/danger surface (primary buttons).
    QString stateAdded, stateModified, stateDeleted, stateUntracked, stateConflict, stateIncoming;
    QString shadow; // Translucent drop-shadow colour for overlay elevation (§9).
    QString focusBorder;
    // Text selection in the diff view. selectionBg is deliberately translucent so
    // syntax highlighting still reads through a selected run (#AARRGGBB).
    QString selectionBg, selectionText;
};

Theme darkTheme();
Theme lightTheme();

} // namespace gittide::ui
