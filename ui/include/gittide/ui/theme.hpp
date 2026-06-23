#pragma once
#include <QString>

namespace gittide::ui {

// A resolved set of design tokens (one theme). Values come from
// docs/superpowers/specs/2026-06-17-visual-design-system.md §2.
struct Theme
{
    bool dark;
    QString surfaceBase, surfaceRaised, surfaceOverlay, border;
    QString textPrimary, textSecondary, textMuted;
    QString accent, accentHover, head;
    QString stateAdded, stateModified, stateDeleted, stateUntracked, stateConflict, stateIncoming;
    QString shadow; // Translucent drop-shadow colour for overlay elevation (§9).
    QString focusBorder;
};

Theme darkTheme();
Theme lightTheme();

} // namespace gittide::ui
