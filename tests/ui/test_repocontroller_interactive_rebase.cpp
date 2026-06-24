#include <QtTest>
#include <QSignalSpy>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>
#include <git2.h>

#include "gittide/ui/repocontroller.hpp"
#include "support/temprepo.hpp"

using gittide::ui::RepoController;

class TestRepoControllerInteractiveRebase : public QObject
{
    Q_OBJECT
private slots:
    void build_todo_lists_range_oldest_first()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("base.txt", "base\n");
        tmp.commitAll("c0");
        tmp.writeFile("a.txt", "a\n");
        tmp.commitAll("A");
        tmp.writeFile("b.txt", "b\n");
        tmp.commitAll("B");

        std::string oidA;
        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r.has_value());
            auto hist = r->log(10);
            QVERIFY(hist.has_value());
            // log() is newest-first: [B, A, c0]; index 1 == A
            oidA = hist->at(1).oid;
        }

        RepoController ctrl;
        ctrl.open(QString::fromStdString(tmp.path().generic_string()));
        QVERIFY(ctrl.isOpen());

        QSignalSpy ready(&ctrl, &RepoController::rebaseTodoReady);
        QSignalSpy failed(&ctrl, &RepoController::operationFailed);

        ctrl.buildRebaseTodo(QString::fromStdString(oidA));

        QTRY_VERIFY_WITH_TIMEOUT(ready.count() > 0 || failed.count() > 0, 5000);

        QCOMPARE(failed.count(), 0);
        QCOMPARE(ready.count(), 1);

        // ready signal: (QString base, QVariantList entries)
        const QVariantList entries = ready.last().at(1).toList();
        QCOMPARE(entries.size(), 2);  // A, B
        QCOMPARE(entries.at(0).toMap().value("summary").toString(), QString("A"));
        QCOMPARE(entries.at(1).toMap().value("summary").toString(), QString("B"));
    }
};

#include "test_repocontroller_interactive_rebase.moc"
