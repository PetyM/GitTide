#include "gittide/ui/theme.hpp"

namespace gittide::ui {

Theme darkTheme()
{
    return Theme{
        .dark           = true,
        .surfaceBase    = QStringLiteral("#1C1C1E"),
        .surfaceRaised  = QStringLiteral("#262628"),
        .surfaceOverlay = QStringLiteral("#333336"),
        .border         = QStringLiteral("#3D3D40"),
        .textPrimary    = QStringLiteral("#E4E4E6"),
        .textSecondary  = QStringLiteral("#A6A6AB"),
        .textMuted      = QStringLiteral("#757579"),
        .accent         = QStringLiteral("#42A5F5"), // Material Blue 400 — reads bright on grey
        .accentHover    = QStringLiteral("#64B5F6"), // Material Blue 300
        .head           = QStringLiteral("#E3F2FD"), // Material Blue 50 — HEAD node, near-white blue
        .stateAdded     = QStringLiteral("#3FB950"),
        .stateModified  = QStringLiteral("#D29922"),
        .stateDeleted   = QStringLiteral("#F85149"),
        .stateUntracked = QStringLiteral("#6E7681"),
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
        .textMuted      = QStringLiteral("#9E9E9E"),
        .accent         = QStringLiteral("#1976D2"), // Material Blue 700
        .accentHover    = QStringLiteral("#1565C0"), // Material Blue 800
        .head           = QStringLiteral("#1976D2"),
        .stateAdded     = QStringLiteral("#3FB950"),
        .stateModified  = QStringLiteral("#D29922"),
        .stateDeleted   = QStringLiteral("#F85149"),
        .stateUntracked = QStringLiteral("#6E7681"),
        .stateConflict  = QStringLiteral("#DB6D28"),
        .stateIncoming  = QStringLiteral("#388BFD"),
        .shadow         = QStringLiteral("#24000000"), // ~14% neutral black
        .focusBorder    = QStringLiteral("#1976D2"),  // = accent
    };
}

} // namespace gittide::ui
