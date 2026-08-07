#pragma once
#include <QObject>
#include <Qt>
#include <optional>

namespace gittide::ui {

/// Combine the two sources of truth for the OS colour scheme.
///
/// The XDG desktop portal's `org.freedesktop.appearance` / `color-scheme` is
/// authoritative when it carries a preference; otherwise Qt's own hint decides.
/// Free function so the rule is testable without a D-Bus session.
///
/// @param portal the portal's preference, or `std::nullopt` when the portal is
///        unreachable, carries no `color-scheme` key, or says "no preference"
/// @param hint   `QStyleHints::colorScheme()`
Qt::ColorScheme resolveSystemColorScheme(std::optional<Qt::ColorScheme> portal, Qt::ColorScheme hint);

/// Source of the OS colour scheme for `ThemeManager`'s `System` mode.
///
/// An interface rather than a plain call so tests can substitute a double and
/// so the D-Bus dependency stays out of `ThemeManager`. Implementations emit
/// changed() when the OS scheme flips under the running app.
class SystemColorScheme : public QObject
{
    Q_OBJECT
public:
    explicit SystemColorScheme(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~SystemColorScheme() override = default;

    virtual Qt::ColorScheme colorScheme() const = 0;

signals:
    /// The OS scheme may have changed; re-read colorScheme().
    void changed();
};

/// Production source: the XDG desktop portal, with `QStyleHints` as fallback.
///
/// Qt alone is not enough on GNOME. Qt picks the **gtk3** platform theme there,
/// which derives the scheme from GTK's `gtk-application-prefer-dark-theme` /
/// theme name — so a session with `color-scheme=prefer-dark` but a light-named
/// GTK theme (or an explicit `gtk-application-prefer-dark-theme=0` in
/// `settings.ini`) is reported as Light and the app opens light on a dark
/// desktop. The portal reports the desktop's real preference, so it wins.
///
/// The portal value is read once at construction and refreshed on the portal's
/// `SettingChanged` signal. Where Qt is built without D-Bus, or the portal is
/// absent (Windows, macOS, a bare X session), this degrades to `QStyleHints`.
class PortalColorScheme : public SystemColorScheme
{
    Q_OBJECT
public:
    explicit PortalColorScheme(QObject* parent = nullptr);

    /// resolveSystemColorScheme(cached portal value, QStyleHints hint).
    Qt::ColorScheme colorScheme() const override;

private slots:
    /// Re-read the portal; emits changed() only when the value actually moved.
    /// Zero-argument on purpose: QtDBus binds a signal to a slot that takes a
    /// prefix of its arguments, so this also serves `SettingChanged(sss)`.
    void refreshFromPortal();

private:
    std::optional<Qt::ColorScheme> m_portal;
};

} // namespace gittide::ui
