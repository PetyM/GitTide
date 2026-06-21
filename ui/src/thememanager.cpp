#include "gittide/ui/thememanager.hpp"

#include <QGuiApplication>
#include <QStyleHints>

namespace gittide::ui {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    // Re-emit live when the OS color scheme changes (only matters in System mode);
    // QML bindings on QmlTheme refresh from themeChanged().
    if (auto* hints = QGuiApplication::styleHints())
    {
        connect(hints,
                &QStyleHints::colorSchemeChanged,
                this,
                [this](Qt::ColorScheme)
                {
                    if (m_mode == Mode::System)
                        emit themeChanged();
                });
    }
}

bool ThemeManager::resolveDark() const
{
    switch (m_mode)
    {
    case Mode::Dark:
        return true;
    case Mode::Light:
        return false;
    case Mode::System:
    default:
    {
        const auto scheme = QGuiApplication::styleHints()->colorScheme();
        // Unknown/Dark → dark (brand's primary look).
        return scheme != Qt::ColorScheme::Light;
    }
    }
}

Theme ThemeManager::currentTheme() const
{
    return resolveDark() ? darkTheme() : lightTheme();
}

QString ThemeManager::iconResource() const
{
    return resolveDark() ? QStringLiteral(":/icons/gittide-icon.svg") : QStringLiteral(":/icons/gittide-icon-light.svg");
}

void ThemeManager::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit themeChanged();
}

} // namespace gittide::ui
