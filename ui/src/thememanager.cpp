#include "gittide/ui/thememanager.hpp"

#include <QApplication>
#include <QIcon>
#include <QStyleHints>

#include "gittide/ui/themestyle.hpp"

namespace gittide::ui {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    // Re-apply live when the OS color scheme changes (only matters in System mode).
    if (auto* hints = QGuiApplication::styleHints())
    {
        connect(hints,
                &QStyleHints::colorSchemeChanged,
                this,
                [this](Qt::ColorScheme)
                {
                    if (m_mode == Mode::System)
                    {
                        if (m_app)
                            applyTo(m_app);
                        emit themeChanged();
                    }
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
    m_mode = mode;
    if (m_app)
        applyTo(m_app);
    emit themeChanged();
}

void ThemeManager::applyTo(QApplication* app)
{
    m_app = app;
    app->setStyleSheet(buildStyleSheet(currentTheme()));
    app->setWindowIcon(QIcon(iconResource()));
}

} // namespace gittide::ui
