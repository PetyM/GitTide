#include "gittide/ui/systemcolorscheme.hpp"

#include <QGuiApplication>
#include <QStyleHints>

#ifdef GITTIDE_HAVE_QTDBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QMetaType>
#include <QVariant>
#endif

namespace gittide::ui {

namespace {

#ifdef GITTIDE_HAVE_QTDBUS

constexpr auto kService   = "org.freedesktop.portal.Desktop";
constexpr auto kPath      = "/org/freedesktop/portal/desktop";
constexpr auto kInterface = "org.freedesktop.portal.Settings";
constexpr auto kNamespace = "org.freedesktop.appearance";
constexpr auto kKey       = "color-scheme";

/// Portal `color-scheme`: 0 = no preference, 1 = prefer dark, 2 = prefer light.
std::optional<Qt::ColorScheme> fromPortalValue(QVariant value)
{
    // `Read` answers a variant wrapping a variant; unwrap until a plain uint.
    while (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();

    bool ok = false;
    switch (value.toUInt(&ok))
    {
    case 1:
        return ok ? std::optional{Qt::ColorScheme::Dark} : std::nullopt;
    case 2:
        return ok ? std::optional{Qt::ColorScheme::Light} : std::nullopt;
    default:
        return std::nullopt; // 0 = no preference, or not a number at all
    }
}

/// One blocking portal round-trip. Short timeout: this runs during startup, and
/// a wedged portal must not hold the first frame hostage.
std::optional<Qt::ColorScheme> readPortal()
{
    if (!QDBusConnection::sessionBus().isConnected())
        return std::nullopt;

    QDBusMessage call = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface), QStringLiteral("Read"));
    call << QLatin1String(kNamespace) << QLatin1String(kKey);

    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return std::nullopt;
    return fromPortalValue(reply.arguments().constFirst());
}

#else

std::optional<Qt::ColorScheme> readPortal()
{
    return std::nullopt;
}

#endif // GITTIDE_HAVE_QTDBUS

} // namespace

Qt::ColorScheme resolveSystemColorScheme(std::optional<Qt::ColorScheme> portal, Qt::ColorScheme hint)
{
    return portal.value_or(hint);
}

PortalColorScheme::PortalColorScheme(QObject* parent)
    : SystemColorScheme(parent)
    , m_portal(readPortal())
{
    // Live OS switches arrive on whichever channel is available: the portal
    // signal where there is one, QStyleHints otherwise.
#ifdef GITTIDE_HAVE_QTDBUS
    QDBusConnection::sessionBus().connect(QLatin1String(kService),
                                          QLatin1String(kPath),
                                          QLatin1String(kInterface),
                                          QStringLiteral("SettingChanged"),
                                          this,
                                          SLOT(refreshFromPortal()));
#endif
    if (auto* hints = QGuiApplication::styleHints())
        connect(hints, &QStyleHints::colorSchemeChanged, this, &SystemColorScheme::changed);
}

Qt::ColorScheme PortalColorScheme::colorScheme() const
{
    auto* hints = QGuiApplication::styleHints();
    return resolveSystemColorScheme(m_portal, hints ? hints->colorScheme() : Qt::ColorScheme::Unknown);
}

void PortalColorScheme::refreshFromPortal()
{
    const auto fresh = readPortal();
    if (fresh == m_portal)
        return;
    m_portal = fresh;
    emit changed();
}

} // namespace gittide::ui
