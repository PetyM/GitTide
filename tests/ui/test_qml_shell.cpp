#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QAbstractItemModel>

#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/thememanager.hpp"
#include "gittide/projectstore.hpp"

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

    void repo_tree_is_bound_to_the_model()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        RepoListModel repoModel;
        std::vector<gittide::RepoRef> repos;
        gittide::RepoRef r;
        r.alias = "gittide";
        r.path  = "/tmp/gittide";
        repos.push_back(r);
        repoModel.setRepos(repos);

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* tree = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("repoTree"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->property("model").value<QAbstractItemModel*>(), &repoModel);
    }
};

#include "test_qml_shell.moc"
