#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtTest>

#include "gittide/projectstore.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

// The add-from-folder dialog must exist in the shell and be reachable from the
// empty state, so the bulk flow is not left orphaned behind a menu item.
class TestQmlAddFromFolder : public QObject
{
    Q_OBJECT
private slots:
    void dialog_and_cta_exist_in_the_shell()
    {
        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        // Mirrors test_qml_shell.cpp's main_qml_loads_without_errors setup: Main.qml
        // reads several context properties (theme, repoModel, ...) beyond
        // projectController, and any missing one makes the load warn.
        ThemeManager    mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme        theme(&mgr);
        RepoListModel   repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        QObject* root = engine.rootObjects().first();
        QVERIFY(root->findChild<QObject*>(QStringLiteral("addFromFolderDialog")) != nullptr);
        QVERIFY(root->findChild<QObject*>(QStringLiteral("addFromFolderCta")) != nullptr);
    }
};

#include "test_qml_add_from_folder.moc"
