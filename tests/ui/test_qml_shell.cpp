#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

class TestQmlShell : public QObject
{
    Q_OBJECT
private slots:
    void main_qml_loads_without_errors()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

        QCOMPARE(engine.rootObjects().size(), 1);
        QCOMPARE(engine.rootObjects().first()->objectName(), QStringLiteral("appWindow"));
    }
};

#include "test_qml_shell.moc"
