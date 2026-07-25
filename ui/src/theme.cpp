#include "gittide/ui/theme.hpp"

namespace gittide::ui {

Theme darkTheme()
{
    return Theme{
        .dark           = true,
        .surfaceBase    = QStringLiteral("#1C1C1E"),
        .surfaceRaised  = QStringLiteral("#262628"),
        .surfaceOverlay = QStringLiteral("#333336"),
        .border         = QStringLiteral("#4A4A4E"), // was #3D3D40 — lifted so dividers, control outlines and the split handles read clearly on the low-contrast dark ground (panels barely separated before)
        .textPrimary    = QStringLiteral("#E4E4E6"),
        .textSecondary  = QStringLiteral("#A6A6AB"),
        .textMuted      = QStringLiteral("#8E8E93"), // 4.6:1 on surface.raised (was #757579, 3.3:1)
        .accent         = QStringLiteral("#42A5F5"), // Material Blue 400 — reads bright on grey
        .accentHover    = QStringLiteral("#64B5F6"), // Material Blue 300
        .head           = QStringLiteral("#E3F2FD"), // Material Blue 50 — HEAD node, near-white blue
        .onAccent       = QStringLiteral("#FFFFFF"), // label on filled accent/danger — white reads crisp on both blue and red
        // Git-state palette: bright-on-dark. Tuned per theme (light darkens these).
        .stateAdded     = QStringLiteral("#3FB950"),
        .stateModified  = QStringLiteral("#D29922"),
        .stateDeleted   = QStringLiteral("#F85149"),
        .stateUntracked = QStringLiteral("#8B949E"), // was #6E7681 — lifted to clear 4.5:1
        .stateConflict  = QStringLiteral("#DB6D28"),
        .stateIncoming  = QStringLiteral("#388BFD"),
        .shadow         = QStringLiteral("#66000000"), // 40% black — deep on dark surfaces
        .focusBorder    = QStringLiteral("#42A5F5"),  // = accent
    };
}

Theme lightTheme()
{
    return Theme{
        .dark           = false,
        .surfaceBase    = QStringLiteral("#F5F5F5"),
        .surfaceRaised  = QStringLiteral("#FFFFFF"),
        .surfaceOverlay = QStringLiteral("#EAEAEA"),
        .border         = QStringLiteral("#E0E0E0"),
        .textPrimary    = QStringLiteral("#212121"),
        .textSecondary  = QStringLiteral("#5F5F5F"),
        .textMuted      = QStringLiteral("#6E6E73"), // 4.7:1 on white (was #9E9E9E, 2.5:1)
        .accent         = QStringLiteral("#1976D2"), // Material Blue 700
        .accentHover    = QStringLiteral("#1565C0"), // Material Blue 800
        .head           = QStringLiteral("#1976D2"),
        .onAccent       = QStringLiteral("#FFFFFF"), // label on filled accent/danger — white in both themes
        // Git-state palette: darkened for the light ground (the dark theme's
        // bright-on-dark values sit at ~2.3–3.1:1 on white). GitHub-light-style.
        .stateAdded     = QStringLiteral("#1A7F37"),
        .stateModified  = QStringLiteral("#9A6700"),
        .stateDeleted   = QStringLiteral("#CF222E"),
        .stateUntracked = QStringLiteral("#6E7781"),
        .stateConflict  = QStringLiteral("#BC4C00"),
        .stateIncoming  = QStringLiteral("#0969DA"),
        .shadow         = QStringLiteral("#24000000"), // ~14% neutral black
        .focusBorder    = QStringLiteral("#1976D2"),  // = accent
    };
}

} // namespace gittide::ui
