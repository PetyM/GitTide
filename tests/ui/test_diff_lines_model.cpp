#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModel>

#include "gittide/ui/difflinesmodel.hpp"
#include "gittide/diff.hpp"

using gittide::ui::DiffLinesModel;

namespace
{
int roleKey(const DiffLinesModel& m, const QByteArray& name)
{
    const auto roles = m.roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}

gittide::DiffResult oneHunkDiff()
{
    gittide::DiffLine ctx;
    ctx.origin    = gittide::DiffLineOrigin::Context;
    ctx.oldLineno = 1;
    ctx.newLineno = 1;
    ctx.text      = "ctx";
    gittide::DiffLine added;
    added.origin    = gittide::DiffLineOrigin::Added;
    added.oldLineno = -1;
    added.newLineno = 2;
    added.text      = "new";
    gittide::DiffLine removed;
    removed.origin    = gittide::DiffLineOrigin::Removed;
    removed.oldLineno = 2;
    removed.newLineno = -1;
    removed.text      = "old";

    gittide::DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 2;
    h.newStart = 1;
    h.newLines = 2;
    h.lines    = {ctx, added, removed};

    gittide::DiffResult r;
    r.hunks = {h};
    return r;
}
}

class TestDiffLinesModel : public QObject
{
    Q_OBJECT
private slots:
    void flattens_hunk_header_plus_lines()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, false);

        // 1 hunk header + 3 lines
        QCOMPARE(m.rowCount(QModelIndex()), 4);

        const int kind      = roleKey(m, "lineKind");
        const int checkable = roleKey(m, "checkable");

        QCOMPARE(m.data(m.index(0, 0), kind).toString(), QStringLiteral("hunk"));
        QCOMPARE(m.data(m.index(1, 0), kind).toString(), QStringLiteral("context"));
        QCOMPARE(m.data(m.index(2, 0), kind).toString(), QStringLiteral("added"));
        QCOMPARE(m.data(m.index(3, 0), kind).toString(), QStringLiteral("removed"));

        QCOMPARE(m.data(m.index(0, 0), checkable).toBool(), false);
        QCOMPARE(m.data(m.index(1, 0), checkable).toBool(), false);
        QCOMPARE(m.data(m.index(2, 0), checkable).toBool(), true);
        QCOMPARE(m.data(m.index(3, 0), checkable).toBool(), true);

        QCOMPARE(m.checkableCount(), 2);
    }

    void whole_checked_marks_all_changed_lines()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true);
        QCOMPARE(m.checkedCount(), 2);
        const auto checked = m.checkedLines();
        QCOMPARE(static_cast<int>(checked.size()), 1);
        QCOMPARE(static_cast<int>(checked.at(0).size()), 2); // both changed lines of hunk 0
    }

    void toggling_a_line_emits_linetoggled_and_updates_checked()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, false);
        QCOMPARE(m.checkedCount(), 0);

        QSignalSpy spy(&m, &DiffLinesModel::lineToggled);
        m.setLineChecked(2, true); // the "added" row

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0); // hunkIndex
        QCOMPARE(spy.at(0).at(1).toInt(), 1); // lineIndex within hunk (the 2nd line)
        QCOMPARE(spy.at(0).at(2).toBool(), true);
        QCOMPARE(m.checkedCount(), 1);
    }

    void clear_empties_the_model()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true);
        m.clear();
        QCOMPARE(m.rowCount(QModelIndex()), 0);
        QCOMPARE(m.checkableCount(), 0);
    }
};

#include "test_diff_lines_model.moc"
