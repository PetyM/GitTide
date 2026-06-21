#include <QtTest>
#include <QColor>
#include <QSignalSpy>

#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;

class TestQmlTheme : public QObject
{
    Q_OBJECT
private slots:
    void dark_tokens_match_theme()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        QVERIFY(theme.property("dark").toBool());
        QCOMPARE(theme.property("accent").value<QColor>(), QColor("#22D3EE"));
        QCOMPARE(theme.property("surfaceBase").value<QColor>(), QColor("#0B1623"));
        QCOMPARE(theme.property("head").value<QColor>(), QColor("#FFFFFF"));
        QCOMPARE(theme.property("iconSource").toString(),
                 QStringLiteral("qrc:/icons/gittide-icon.svg"));
    }

    void shadow_token_is_exposed_and_translucent()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        const QColor s = theme.property("shadow").value<QColor>();
        QVERIFY(s.isValid());
        QVERIFY(s.alpha() < 255);
    }

    void lane_palette_is_five_hues_starting_cyan()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        const QVariantList lanes = theme.property("laneColors").toList();
        QCOMPARE(lanes.size(), 5);
        QCOMPARE(lanes.front().value<QColor>(), QColor("#22D3EE"));
        QCOMPARE(lanes.at(1).value<QColor>(), QColor("#A371F7"));
    }

    void theme_switch_emits_changed_and_updates_tokens()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QSignalSpy spy(&theme, &QmlTheme::changed);

        mgr.setMode(ThemeManager::Mode::Light);

        QCOMPARE(spy.count(), 1);
        QVERIFY(!theme.property("dark").toBool());
        QCOMPARE(theme.property("accent").value<QColor>(), QColor("#0891B2"));
    }

    // The QML toggle drives the mode through QmlTheme: a writable `mode` property
    // (int mirroring ThemeManager::Mode) and a cycleMode() invokable.
    void mode_property_round_trips_and_emits_changed()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::System);
        QmlTheme theme(&mgr);
        QSignalSpy spy(&theme, &QmlTheme::changed);

        QCOMPARE(theme.property("mode").toInt(), int(ThemeManager::Mode::System));
        QVERIFY(theme.setProperty("mode", int(ThemeManager::Mode::Light)));

        QCOMPARE(theme.property("mode").toInt(), int(ThemeManager::Mode::Light));
        QVERIFY(!theme.property("dark").toBool());
        QCOMPARE(spy.count(), 1);
    }

    void cycle_mode_rotates_system_dark_light()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::System);
        QmlTheme theme(&mgr);

        QMetaObject::invokeMethod(&theme, "cycleMode");
        QCOMPARE(theme.property("mode").toInt(), int(ThemeManager::Mode::Dark));
        QMetaObject::invokeMethod(&theme, "cycleMode");
        QCOMPARE(theme.property("mode").toInt(), int(ThemeManager::Mode::Light));
        QMetaObject::invokeMethod(&theme, "cycleMode");
        QCOMPARE(theme.property("mode").toInt(), int(ThemeManager::Mode::System));
    }
};

#include "test_qml_theme.moc"
