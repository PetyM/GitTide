#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QListWidget>
#include <QPushButton>

#include "gittide/ui/DiffView.hpp"
#include "gittide/ui/Metatypes.hpp"

using gittide::ui::DiffView;

namespace diff_view_test {
gittide::DiffResult two_added_one_context() {
    gittide::DiffHunk h;
    h.oldStart = 1; h.oldLines = 1; h.newStart = 1; h.newLines = 3;
    h.lines = {
        {gittide::DiffLineOrigin::Added,   -1, 1, "alpha", false},
        {gittide::DiffLineOrigin::Added,   -1, 2, "beta",  false},
        {gittide::DiffLineOrigin::Context,  1, 3, "gamma", false},
    };
    return gittide::DiffResult{.hunks = {h}};
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
        const auto sel = spy.at(0).at(0).value<gittide::StageSelection>();
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

    void stage_button_click_emits_request() {
        DiffView view;
        view.setDiff(diff_view_test::two_added_one_context(), "f.txt");
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        list->item(0)->setSelected(true);
        auto* stageBtn = view.findChild<QPushButton*>(QStringLiteral("diffStageButton"));
        QVERIFY(stageBtn != nullptr);

        QSignalSpy spy(&view, &DiffView::stageRequested);
        stageBtn->click();

        QCOMPARE(spy.count(), 1);
        const auto sel = spy.at(0).at(0).value<gittide::StageSelection>();
        QCOMPARE(static_cast<int>(sel.lineIndices.size()), 1);
        QCOMPARE(sel.lineIndices[0], 0);
    }
};

#include "test_diff_view.moc"
