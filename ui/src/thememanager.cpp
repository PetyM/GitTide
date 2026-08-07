#include "gittide/ui/thememanager.hpp"

namespace gittide::ui {

ThemeManager::ThemeManager(QObject* parent)
    : ThemeManager(new PortalColorScheme, parent)
{
    m_source->setParent(this); // the default source is ours to own
}

ThemeManager::ThemeManager(SystemColorScheme* source, QObject* parent)
    : QObject(parent)
    , m_source(source)
{
    // Re-emit live when the OS color scheme changes (only matters in System mode);
    // QML bindings on QmlTheme refresh from themeChanged().
    connect(m_source,
            &SystemColorScheme::changed,
            this,
            [this]
            {
                if (m_mode == Mode::System)
                    emit themeChanged();
            });
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
        // Unknown/Dark → dark (brand's primary look).
        return m_source->colorScheme() != Qt::ColorScheme::Light;
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
