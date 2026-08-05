// Tests for DiffSelection — the character-accurate, cross-row text selection
// behind the diff view. Pure logic over a DiffLinesModel; no window involved.

#include <QtTest>
#include <QSignalSpy>

#include "gittide/diff.hpp"
#include "gittide/ui/diffselection.hpp"
#include "gittide/ui/difflinesmodel.hpp"

using gittide::ui::DiffLinesModel;
using gittide::ui::DiffSelection;

namespace
{
// One hunk: "@@ -1,2 +1,2 @@" / "ctx one" / "added two" / "removed three".
// Rows: 0 = hunk header, 1 = context, 2 = added, 3 = removed.
gittide::DiffResult sampleDiff()
{
    gittide::DiffLine ctx;
    ctx.origin    = gittide::DiffLineOrigin::Context;
    ctx.oldLineno = 1;
    ctx.newLineno = 1;
    ctx.text      = "ctx one";

    gittide::DiffLine added;
    added.origin    = gittide::DiffLineOrigin::Added;
    added.oldLineno = -1;
    added.newLineno = 2;
    added.text      = "added two";

    gittide::DiffLine removed;
    removed.origin    = gittide::DiffLineOrigin::Removed;
    removed.oldLineno = 2;
    removed.newLineno = -1;
    removed.text      = "removed three";

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
} // namespace

class TestDiffSelection : public QObject
{
    Q_OBJECT
private slots:

    void no_selection_before_any_press()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        QVERIFY(!sel.property("hasSelection").toBool());
        QCOMPARE(sel.startInRow(1), -1);
        QCOMPARE(sel.endInRow(1), -1);
    }

    void a_press_without_a_drag_is_not_a_selection()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 3);
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void forward_drag_covers_partial_first_and_last_rows()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 4);   // "ctx |one"
        sel.extendTo(3, 7); // "removed| three"

        QVERIFY(sel.property("hasSelection").toBool());
        QCOMPARE(sel.startInRow(1), 4);
        QCOMPARE(sel.endInRow(1), 7);       // "ctx one".size()
        QCOMPARE(sel.startInRow(2), 0);     // whole middle row
        QCOMPARE(sel.endInRow(2), 9);       // "added two".size()
        QCOMPARE(sel.startInRow(3), 0);
        QCOMPARE(sel.endInRow(3), 7);
        QCOMPARE(sel.startInRow(0), -1);    // hunk header is outside
    }

    void backward_drag_normalises_to_the_same_range()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection forward;
        forward.setModel(&model);
        DiffSelection backward;
        backward.setModel(&model);

        forward.begin(1, 4);
        forward.extendTo(3, 7);
        backward.begin(3, 7);
        backward.extendTo(1, 4);

        for (int row = 0; row < 4; ++row)
        {
            QCOMPARE(backward.startInRow(row), forward.startInRow(row));
            QCOMPARE(backward.endInRow(row), forward.endInRow(row));
        }
    }

    void right_to_left_drag_inside_one_row_normalises()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(2, 7);
        sel.extendTo(2, 2);
        QCOMPARE(sel.startInRow(2), 2);
        QCOMPARE(sel.endInRow(2), 7);
    }

    void columns_are_clamped_to_the_row_length()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 0);
        sel.extendTo(1, 999);
        QCOMPARE(sel.endInRow(1), 7);
        QCOMPARE(sel.rowLength(1), 7);
    }

    void select_all_spans_first_column_to_last_row_end()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectAll();
        QVERIFY(sel.property("hasSelection").toBool());
        QCOMPARE(sel.startInRow(0), 0);
        QCOMPARE(sel.endInRow(3), 13); // "removed three".size()
    }

    void select_all_on_an_empty_model_selects_nothing()
    {
        DiffLinesModel model;
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectAll();
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void a_model_reset_clears_the_selection()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);
        sel.selectAll();
        QVERIFY(sel.property("hasSelection").toBool());

        model.setDiff(sampleDiff(), {}, false); // a new file lands in the same model
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void changing_the_model_clears_and_notifies()
    {
        DiffLinesModel first;
        first.setDiff(sampleDiff(), {}, false);
        DiffLinesModel second;
        second.setDiff(sampleDiff(), {}, false);

        DiffSelection sel;
        sel.setModel(&first);
        sel.selectAll();
        QSignalSpy modelSpy(&sel, SIGNAL(modelChanged()));

        sel.setModel(&second);
        QCOMPARE(modelSpy.count(), 1);
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void anchor_row_is_readable_for_drag_direction()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(2, 0);
        QCOMPARE(sel.property("anchorRow").toInt(), 2);
    }
};

#include "test_diff_selection.moc"
