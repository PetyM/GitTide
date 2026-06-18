#pragma once
#include <QPalette>
#include <QString>

#include "gittide/ui/theme.hpp"

namespace gittide::ui {

/// Maps theme tokens to a QPalette for use with the Fusion base style.
/// Every palette role is driven by a token; no hex literals are used here.
QPalette buildPalette(const Theme& theme);

/// Returns the small accent QSS that covers cues a palette cannot express:
/// QTabBar underline, selection left-border, focus ring, and diff gutter colours.
/// No hex literals — every colour is interpolated from a token.
QString buildAccentStyleSheet(const Theme& theme);

} // namespace gittide::ui
