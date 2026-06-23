#include "gittide/ui/qmltheme.hpp"

namespace gittide::ui {

QmlTheme::QmlTheme(ThemeManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
{
    connect(m_manager, &ThemeManager::themeChanged, this, &QmlTheme::changed);
}

Theme QmlTheme::theme() const
{
    return m_manager->currentTheme();
}

bool QmlTheme::dark() const
{
    return theme().dark;
}

int QmlTheme::mode() const
{
    return static_cast<int>(m_manager->mode());
}

void QmlTheme::setMode(int mode)
{
    const auto m = static_cast<ThemeManager::Mode>(mode);
    if (m == m_manager->mode())
        return;
    m_manager->setMode(m); // emits themeChanged → our changed()
}

void QmlTheme::cycleMode()
{
    // System → Dark → Light → System.
    switch (m_manager->mode())
    {
    case ThemeManager::Mode::System:
        setMode(static_cast<int>(ThemeManager::Mode::Dark));
        break;
    case ThemeManager::Mode::Dark:
        setMode(static_cast<int>(ThemeManager::Mode::Light));
        break;
    case ThemeManager::Mode::Light:
        setMode(static_cast<int>(ThemeManager::Mode::System));
        break;
    }
}

QColor QmlTheme::surfaceBase() const
{
    return QColor(theme().surfaceBase);
}
QColor QmlTheme::surfaceRaised() const
{
    return QColor(theme().surfaceRaised);
}
QColor QmlTheme::surfaceOverlay() const
{
    return QColor(theme().surfaceOverlay);
}
QColor QmlTheme::border() const
{
    return QColor(theme().border);
}
QColor QmlTheme::textPrimary() const
{
    return QColor(theme().textPrimary);
}
QColor QmlTheme::textSecondary() const
{
    return QColor(theme().textSecondary);
}
QColor QmlTheme::textMuted() const
{
    return QColor(theme().textMuted);
}
QColor QmlTheme::accent() const
{
    return QColor(theme().accent);
}
QColor QmlTheme::accentHover() const
{
    return QColor(theme().accentHover);
}
QColor QmlTheme::head() const
{
    return QColor(theme().head);
}
QColor QmlTheme::stateAdded() const
{
    return QColor(theme().stateAdded);
}
QColor QmlTheme::stateModified() const
{
    return QColor(theme().stateModified);
}
QColor QmlTheme::stateDeleted() const
{
    return QColor(theme().stateDeleted);
}
QColor QmlTheme::stateUntracked() const
{
    return QColor(theme().stateUntracked);
}
QColor QmlTheme::stateConflict() const
{
    return QColor(theme().stateConflict);
}
QColor QmlTheme::stateIncoming() const
{
    return QColor(theme().stateIncoming);
}
QColor QmlTheme::shadow() const
{
    return QColor(theme().shadow);
}
QColor QmlTheme::focusBorder() const
{
    return QColor(theme().focusBorder);
}

QVariantList QmlTheme::laneColors() const
{
    // Graph lane palette — the one documented exception to single-accent.
    static const QVariantList s_lanes = {
        QColor("#22D3EE"), QColor("#A371F7"), QColor("#3FB950"),
        QColor("#D29922"), QColor("#F778BA")};
    return s_lanes;
}

QString QmlTheme::iconSource() const
{
    // ThemeManager returns a ":/…" resource path; QML wants the "qrc:/…" form.
    return QStringLiteral("qrc") + m_manager->iconResource();
}

} // namespace gittide::ui
