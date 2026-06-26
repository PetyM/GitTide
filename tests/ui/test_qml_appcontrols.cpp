// Tests for AppButton.qml — the themed action button (primary/secondary/danger + compact).
// Loads the component via its QRC URL so the QML engine can resolve the qrc:/qml
// path where same-dir App* types live (bare setData without a base URL cannot
// resolve those types).

#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <memory>

#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;

class TestQmlAppControls : public QObject
{
    Q_OBJECT
private slots:

    void app_button_instantiates_all_variants_and_compact()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        // Each variant must load without error and pass properties through.
        for (const QString& v : {QStringLiteral("primary"),
                                  QStringLiteral("secondary"),
                                  QStringLiteral("danger")})
        {
            QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/AppButton.qml")));
            QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
            std::unique_ptr<QObject> obj(comp.create());
            QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));
            obj->setProperty("text", QStringLiteral("X"));
            obj->setProperty("objectName", QStringLiteral("b"));
            obj->setProperty("variant", v);
            QCOMPARE(obj->property("text").toString(), QStringLiteral("X"));
            QCOMPARE(obj->objectName(), QStringLiteral("b"));
            QCOMPARE(obj->property("variant").toString(), v);
        }

        // compact: true must produce a smaller implicitHeight than the default.
        QQmlComponent regComp(&engine, QUrl(QStringLiteral("qrc:/qml/AppButton.qml")));
        QVERIFY2(regComp.errorString().isEmpty(), qPrintable(regComp.errorString()));
        std::unique_ptr<QObject> regular(regComp.create());
        QVERIFY2(regular != nullptr, qPrintable(regComp.errorString()));

        QQmlComponent cmpComp(&engine, QUrl(QStringLiteral("qrc:/qml/AppButton.qml")));
        QVERIFY2(cmpComp.errorString().isEmpty(), qPrintable(cmpComp.errorString()));
        std::unique_ptr<QObject> compact(cmpComp.create());
        QVERIFY2(compact != nullptr, qPrintable(cmpComp.errorString()));
        compact->setProperty("compact", true);

        const qreal regularH = regular->property("implicitHeight").toReal();
        const qreal compactH = compact->property("implicitHeight").toReal();
        QVERIFY2(compactH < regularH,
                 qPrintable(QStringLiteral("compact implicitHeight (%1) should be < regular (%2)")
                                .arg(compactH)
                                .arg(regularH)));
    }
};

#include "test_qml_appcontrols.moc"
