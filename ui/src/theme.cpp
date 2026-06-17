#include "gittide/ui/theme.hpp"

namespace gittide::ui {

Theme darkTheme() {
    return Theme{
        .dark           = true,
        .surfaceBase    = QStringLiteral("#0B1623"),
        .surfaceRaised  = QStringLiteral("#11202F"),
        .surfaceOverlay = QStringLiteral("#16293B"),
        .border         = QStringLiteral("#1E3245"),
        .textPrimary    = QStringLiteral("#C9D1D9"),
        .textSecondary  = QStringLiteral("#8B949E"),
        .textMuted      = QStringLiteral("#6E7681"),
        .accent         = QStringLiteral("#22D3EE"),
        .accentHover    = QStringLiteral("#4DDFF2"),
        .head           = QStringLiteral("#FFFFFF"),
        .stateAdded     = QStringLiteral("#3FB950"),
        .stateModified  = QStringLiteral("#D29922"),
        .stateDeleted   = QStringLiteral("#F85149"),
        .stateUntracked = QStringLiteral("#6E7681"),
        .stateConflict  = QStringLiteral("#DB6D28"),
    };
}

Theme lightTheme() {
    return Theme{
        .dark           = false,
        .surfaceBase    = QStringLiteral("#EEF3F8"),
        .surfaceRaised  = QStringLiteral("#FFFFFF"),
        .surfaceOverlay = QStringLiteral("#F4F8FB"),
        .border         = QStringLiteral("#D4DFEA"),
        .textPrimary    = QStringLiteral("#0B1623"),
        .textSecondary  = QStringLiteral("#51606E"),
        .textMuted      = QStringLiteral("#8595A4"),
        .accent         = QStringLiteral("#0891B2"),
        .accentHover    = QStringLiteral("#0AA5CC"),
        .head           = QStringLiteral("#0891B2"),
        .stateAdded     = QStringLiteral("#3FB950"),
        .stateModified  = QStringLiteral("#D29922"),
        .stateDeleted   = QStringLiteral("#F85149"),
        .stateUntracked = QStringLiteral("#6E7681"),
        .stateConflict  = QStringLiteral("#DB6D28"),
    };
}

}  // namespace gittide::ui
