#include "gittide/ui/thememanager.hpp"

#include <QApplication>
#include <QIcon>
#include <QStyle>
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

    // Set Fusion as the base style once; it renders correctly from a QPalette
    // and provides a consistent cross-platform baseline (D24/D25).
    if (!app->style() || app->style()->objectName() != QStringLiteral("fusion"))
        app->setStyle(QStringLiteral("Fusion"));

    const Theme t = currentTheme();
    app->setPalette(buildPalette(t));
    app->setStyleSheet(buildAccentStyleSheet(t));

    // Publish the per-state status-letter colours as dynamic app properties so
    // ChangedFilesList can tint A/M/D/U/C letters from the active theme (it reads
    // these "gittide.state*" keys; falls back to gray when unset).
    app->setProperty("gittide.stateAdded", t.stateAdded);
    app->setProperty("gittide.stateModified", t.stateModified);
    app->setProperty("gittide.stateDeleted", t.stateDeleted);
    app->setProperty("gittide.stateUntracked", t.stateUntracked);
    app->setProperty("gittide.stateConflict", t.stateConflict);

    app->setWindowIcon(QIcon(iconResource()));
}

} // namespace gittide::ui
