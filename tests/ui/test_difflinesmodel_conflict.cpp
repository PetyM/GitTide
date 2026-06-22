#include <QtTest>
#include "gittide/ui/difflinesmodel.hpp"

using gittide::ui::DiffLinesModel;

static const QString kConflict =
    "line1\n"
    "<<<<<<< HEAD\n"
    "ours\n"
    "=======\n"
    "theirs\n"
    ">>>>>>> feature\n"
    "line2\n";

class TestDiffConflict : public QObject
{
    Q_OBJECT
private slots:
    void parses_one_region()
    {
        DiffLinesModel m;
        m.setConflictContent(kConflict);
        QVERIFY(!m.isResolved()); // markers present
    }

    void accept_current_keeps_ours()
    {
        DiffLinesModel m;
        m.setConflictContent(kConflict);
        const QString out = m.acceptCurrent(0);
        QCOMPARE(out, QStringLiteral("line1\nours\nline2\n"));
        m.setConflictContent(out);
        QVERIFY(m.isResolved());
    }

    void accept_incoming_keeps_theirs()
    {
        DiffLinesModel m;
        m.setConflictContent(kConflict);
        QCOMPARE(m.acceptIncoming(0), QStringLiteral("line1\ntheirs\nline2\n"));
    }

    void accept_both_keeps_both()
    {
        DiffLinesModel m;
        m.setConflictContent(kConflict);
        QCOMPARE(m.acceptBoth(0), QStringLiteral("line1\nours\ntheirs\nline2\n"));
    }
};

#include "test_difflinesmodel_conflict.moc"
