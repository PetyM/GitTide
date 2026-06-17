#include <QObject>
#include <QtTest/QtTest>
#include <QAbstractItemModelTester>

#include "gittide/ProjectStore.hpp"
#include "gittide/ui/ProjectListModel.hpp"

using gittide::Project;
using gittide::ProjectStore;
using gittide::ui::ProjectListModel;

class TestProjectListModel : public QObject {
    Q_OBJECT
private slots:
    void exposes_names_and_ids() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        store.projects().push_back(Project{.id = "id-b", .name = "Home"});

        ProjectListModel model(&store);
        QAbstractItemModelTester tester(&model);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("Work"));
        QCOMPARE(model.data(model.index(1), ProjectListModel::IdRole).toString(), QStringLiteral("id-b"));
    }

    void refresh_picks_up_new_projects() {
        ProjectStore store;
        ProjectListModel model(&store);
        QCOMPARE(model.rowCount(), 0);

        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        model.refresh();
        QCOMPARE(model.rowCount(), 1);
    }
};

#include "test_project_list_model.moc"
