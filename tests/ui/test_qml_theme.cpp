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
};

#include "test_qml_theme.moc"
