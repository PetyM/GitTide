#include <QtTest>

#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

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
};

#include "test_theme_manager.moc"
