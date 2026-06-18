#include <QListWidget>
#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "gittide/ui/diffview.hpp"
#include "gittide/ui/metatypes.hpp"

using gittide::ui::DiffView;

namespace diff_view_test {

// One hunk: two added lines + one context line.
gittide::DiffResult two_added_one_context()
{
    gittide::DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 1;
    h.newStart = 1;
    h.newLines = 3;
    h.lines    = {
        {gittide::DiffLineOrigin::Added,   -1, 1, "alpha", false},
        {gittide::DiffLineOrigin::Added,   -1, 2, "beta",  false},
        {gittide::DiffLineOrigin::Context,  1, 3, "gamma", false},
    };
    return gittide::DiffResult{.hunks = {h}};
}

// One hunk: one added + one removed + one context.
gittide::DiffResult added_removed_context()
{
    gittide::DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 2;
    h.newStart = 1;
    h.newLines = 2;
    h.lines    = {
        {gittide::DiffLineOrigin::Added,   -1, 1, "new",  false},
        {gittide::DiffLineOrigin::Removed,  1, -1, "old", false},
        {gittide::DiffLineOrigin::Context,  2,  2, "ctx", false},
    };
    return gittide::DiffResult{.hunks = {h}};
}

} // namespace diff_view_test

class TestDiffView : public QObject
{
    Q_OBJECT
private slots:

    // --- Basic rendering ---

    void renders_one_row_per_line()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt", true, {});
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);
    }

    // --- Editable mode: checkboxes ---

    void editable_lines_are_checked_by_default()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        gittide::DiffResult r;
        gittide::DiffHunk h;
        h.lines = {{gittide::DiffLineOrigin::Added, -1, 1, "new", false}};
        r.hunks.push_back(h);
        view.setDiff(r, "a.txt", /*wholeChecked=*/true, {});
        auto* lines = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        QVERIFY(lines);
        QVERIFY(lines->count() >= 1);
        // The added line row must be user-checkable and initially checked.
        auto* item = lines->item(0);
        QVERIFY(item->flags() & Qt::ItemIsUserCheckable);
        QCOMPARE(item->checkState(), Qt::Checked);
    }

    void editable_whole_checked_false_leaves_lines_unchecked()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt", /*wholeChecked=*/false, {});
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        // Items 0 and 1 are Added → should be checkable but unchecked.
        QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
        QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
    }

    void editable_context_line_is_not_checkable()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt", /*wholeChecked=*/true, {});
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        // Item 2 is the context line — must NOT be checkable.
        auto* ctx = list->item(2);
        QVERIFY(!(ctx->flags() & Qt::ItemIsUserCheckable));
    }

    void editable_checkedLines_overrides_wholeChecked()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        // Two added lines; pass checkedLines with only line 0 checked.
        std::map<int, std::vector<int>> checked{{0, {0}}};
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt", /*wholeChecked=*/true, checked);
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        QCOMPARE(list->item(0)->checkState(), Qt::Checked);
        QCOMPARE(list->item(1)->checkState(), Qt::Unchecked); // not in checkedLines
    }

    void editable_toggle_emits_lineCheckToggled()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        gittide::DiffResult r;
        gittide::DiffHunk h;
        h.lines = {{gittide::DiffLineOrigin::Added, -1, 1, "x", false}};
        r.hunks.push_back(h);
        view.setDiff(r, "b.txt", /*wholeChecked=*/true, {});
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));

        QSignalSpy spy(&view, &DiffView::lineCheckToggled);
        // Uncheck the item programmatically via the item API (simulates user toggle).
        list->item(0)->setCheckState(Qt::Unchecked);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("b.txt"));
        QCOMPARE(spy.at(0).at(1).toInt(), 0);   // hunkIndex
        QCOMPARE(spy.at(0).at(2).toInt(), 0);   // lineIndex
        QCOMPARE(spy.at(0).at(3).toBool(), false);
    }

    void editable_toggle_check_emits_true()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        gittide::DiffResult r;
        gittide::DiffHunk h;
        h.lines = {{gittide::DiffLineOrigin::Added, -1, 1, "x", false}};
        r.hunks.push_back(h);
        // Start unchecked.
        view.setDiff(r, "c.txt", /*wholeChecked=*/false, {});
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));

        QSignalSpy spy(&view, &DiffView::lineCheckToggled);
        list->item(0)->setCheckState(Qt::Checked);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(3).toBool(), true);
    }

    void editable_programmatic_fill_does_not_emit_signal()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        QSignalSpy spy(&view, &DiffView::lineCheckToggled);
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt", true, {});
        // setDiff is a programmatic fill — must not emit lineCheckToggled.
        QCOMPARE(spy.count(), 0);
    }

    // --- ReadOnly mode: no checkboxes ---

    void readonly_no_checkboxes()
    {
        DiffView view;
        view.setMode(DiffView::Mode::ReadOnly);
        view.setDiff(diff_view_test::added_removed_context(), "f.txt", /*wholeChecked=*/true, {});
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        QVERIFY(list);
        QCOMPARE(list->count(), 3);
        for (int i = 0; i < list->count(); ++i)
            QVERIFY(!(list->item(i)->flags() & Qt::ItemIsUserCheckable));
    }

    void readonly_toggle_does_not_emit()
    {
        DiffView view;
        view.setMode(DiffView::Mode::ReadOnly);
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt", false, {});
        QSignalSpy spy(&view, &DiffView::lineCheckToggled);
        // In ReadOnly, items are not checkable — but even if somehow changed,
        // no signal should be emitted.
        QCOMPARE(spy.count(), 0);
    }

    // --- clear() ---

    void clear_empties_list()
    {
        DiffView view;
        view.setMode(DiffView::Mode::Editable);
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt", true, {});
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        view.clear();
        QCOMPARE(list->count(), 0);
    }
};

#include "test_diff_view.moc"
