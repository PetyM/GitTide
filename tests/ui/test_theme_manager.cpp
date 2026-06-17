#include <QtTest>
#include <QApplication>
#include "gittide/ui/ThemeManager.hpp"

using namespace gittide::ui;

class TestThemeManager : public QObject {
    Q_OBJECT
private slots:
    void forced_dark_resolves_to_dark_theme() {
        ThemeManager m;
        m.setMode(ThemeManager::Mode::Dark);
        QVERIFY(m.currentTheme().dark);
        QCOMPARE(m.currentTheme().accent, QStringLiteral("#22D3EE"));
        QVERIFY(m.iconResource().contains(QStringLiteral("gittide-icon.svg")));
        QVERIFY(!m.iconResource().contains(QStringLiteral("light")));
    }
    void forced_light_resolves_to_light_theme() {
        ThemeManager m;
        QSignalSpy spy(&m, &ThemeManager::themeChanged);
        m.setMode(ThemeManager::Mode::Light);
        QVERIFY(!m.currentTheme().dark);
        QCOMPARE(m.currentTheme().accent, QStringLiteral("#0891B2"));
        QVERIFY(m.iconResource().contains(QStringLiteral("light")));
        QCOMPARE(spy.count(), 1);
    }
    void applyTo_sets_nonempty_stylesheet() {
        ThemeManager m;
        m.setMode(ThemeManager::Mode::Dark);
        m.applyTo(qApp);
        QVERIFY(!qApp->styleSheet().isEmpty());
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("#22D3EE")));
        qApp->setStyleSheet(QString());  // reset for other tests
    }
};

#include "test_theme_manager.moc"
