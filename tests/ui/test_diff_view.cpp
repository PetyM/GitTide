#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QListWidget>

#include "gitgui/ui/DiffView.hpp"
#include "gitgui/ui/Metatypes.hpp"

using gitgui::ui::DiffView;

namespace diff_view_test {
gitgui::DiffResult two_added_one_context() {
    gitgui::DiffHunk h;
    h.oldStart = 1; h.oldLines = 1; h.newStart = 1; h.newLines = 3;
    h.lines = {
        {gitgui::DiffLineOrigin::Added,   -1, 1, "alpha", false},
        {gitgui::DiffLineOrigin::Added,   -1, 2, "beta",  false},
        {gitgui::DiffLineOrigin::Context,  1, 3, "gamma", false},
    };
    return gitgui::DiffResult{.hunks = {h}};
}
}  // namespace diff_view_test

class TestDiffView : public QObject {
    Q_OBJECT
private slots:
    void renders_one_row_per_line() {
        DiffView view;
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt");
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);
    }

    void selecting_lines_builds_selection_and_emits() {
        DiffView view;
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt");
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        list->item(0)->setSelected(true);
        list->item(1)->setSelected(true);

        QSignalSpy spy(&view, &DiffView::stageRequested);
        view.requestStage();

        QCOMPARE(spy.count(), 1);
        const auto sel = spy.at(0).at(0).value<gitgui::StageSelection>();
        QCOMPARE(sel.path, std::filesystem::path("f.txt"));
        QVERIFY(sel.hunkIndex.has_value());
        QCOMPARE(sel.hunkIndex.value(), 0);
        QCOMPARE(static_cast<int>(sel.lineIndices.size()), 2);
        QCOMPARE(sel.lineIndices[0], 0);
        QCOMPARE(sel.lineIndices[1], 1);
    }

    void no_selection_yields_nullopt() {
        DiffView view;
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt");
        QVERIFY(!view.currentSelection().has_value());
    }
};

#include "test_diff_view.moc"
