#include <QtTest>

#include "gittide/ui/systemcolorscheme.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

namespace {

/// Test double for the OS colour-scheme source: reports whatever the test sets,
/// so `System` mode can be exercised without a D-Bus session or a real desktop.
class FakeColorScheme : public SystemColorScheme
{
public:
    Qt::ColorScheme colorScheme() const override
    {
        return m_scheme;
    }
    void set(Qt::ColorScheme scheme)
    {
        m_scheme = scheme;
        emit changed();
    }

private:
    Qt::ColorScheme m_scheme = Qt::ColorScheme::Unknown;
};

} // namespace

class TestThemeManager : public QObject
{
    Q_OBJECT
private slots:
    void forced_dark_resolves_to_dark_theme()
    {
        ThemeManager m;
        m.setMode(ThemeManager::Mode::Dark);
        QVERIFY(m.currentTheme().dark);
        QCOMPARE(m.currentTheme().accent, QStringLiteral("#42A5F5"));
        QVERIFY(m.iconResource().contains(QStringLiteral("gittide-icon.svg")));
        QVERIFY(!m.iconResource().contains(QStringLiteral("light")));
    }
    void forced_light_resolves_to_light_theme()
    {
        ThemeManager m;
        QSignalSpy spy(&m, &ThemeManager::themeChanged);
        m.setMode(ThemeManager::Mode::Light);
        QVERIFY(!m.currentTheme().dark);
        QCOMPARE(m.currentTheme().accent, QStringLiteral("#1976D2"));
        QVERIFY(m.iconResource().contains(QStringLiteral("light")));
        QCOMPARE(spy.count(), 1);
    }
    void system_mode_follows_the_source()
    {
        FakeColorScheme source;
        ThemeManager m(&source);
        source.set(Qt::ColorScheme::Light);
        QVERIFY(!m.currentTheme().dark);
        source.set(Qt::ColorScheme::Dark);
        QVERIFY(m.currentTheme().dark);
    }
    void system_mode_treats_unknown_as_dark()
    {
        FakeColorScheme source; // defaults to Unknown
        ThemeManager m(&source);
        QVERIFY(m.currentTheme().dark); // the brand's primary look
    }
    void source_change_re_emits_only_in_system_mode()
    {
        FakeColorScheme source;
        ThemeManager m(&source);
        QSignalSpy spy(&m, &ThemeManager::themeChanged);
        source.set(Qt::ColorScheme::Light);
        QCOMPARE(spy.count(), 1);

        m.setMode(ThemeManager::Mode::Dark); // forced: the OS no longer matters
        spy.clear();
        source.set(Qt::ColorScheme::Light);
        QCOMPARE(spy.count(), 0);
        QVERIFY(m.currentTheme().dark);
    }
    void portal_value_wins_over_the_qt_hint()
    {
        // GNOME with `color-scheme=prefer-dark` but a light-named GTK theme: Qt's
        // gtk3 platform theme reports Light, the portal reports Dark. The portal
        // is authoritative — this is the bug the source exists to fix.
        QCOMPARE(resolveSystemColorScheme(Qt::ColorScheme::Dark, Qt::ColorScheme::Light), Qt::ColorScheme::Dark);
        QCOMPARE(resolveSystemColorScheme(Qt::ColorScheme::Light, Qt::ColorScheme::Dark), Qt::ColorScheme::Light);
    }
    void qt_hint_is_the_fallback_without_a_portal_value()
    {
        // No portal, no `color-scheme` key, or "no preference" (0) → trust Qt.
        QCOMPARE(resolveSystemColorScheme(std::nullopt, Qt::ColorScheme::Light), Qt::ColorScheme::Light);
        QCOMPARE(resolveSystemColorScheme(std::nullopt, Qt::ColorScheme::Dark), Qt::ColorScheme::Dark);
        QCOMPARE(resolveSystemColorScheme(std::nullopt, Qt::ColorScheme::Unknown), Qt::ColorScheme::Unknown);
    }
};

#include "test_theme_manager.moc"
