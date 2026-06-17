#pragma once
#include <QString>
#include "gittide/ui/Theme.hpp"

namespace gittide::ui {

// Builds the application-wide Qt stylesheet (QSS) for the given theme. This is
// the ONLY place color literals are emitted — every value is read from `theme`.
QString buildStyleSheet(const Theme& theme);

}  // namespace gittide::ui
