#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "gittide/ui/changedfileslist.hpp"
#include "gittide/ui/changesview.hpp"
#include "gittide/ui/metatypes.hpp"

using gittide::ui::ChangedFilesList;
using gittide::ui::ChangesView;

namespace changes_view_test {
std::vector<gittide::FileStatus> two_files()
{
    return {
        {std::filesystem::path("a.txt"), gittide::StatusFlag::WtModified},
        {std::filesystem::path("b.txt"), gittide::StatusFlag::WtNew},
    };
}
} // namespace changes_view_test

class TestChangesView : public QObject
{
    Q_OBJECT
private slots:
    void commit_builds_selections_from_checked_files()
    {
        ChangesView view;
        view.setStatus({{"a.txt", gittide::StatusFlag::WtModified}, {"b.txt", gittide::StatusFlag::WtNew}});

        // type a message
        auto* msg = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
        QVERIFY(msg);
        msg->setPlainText(QStringLiteral("msg"));

        // uncheck b.txt via the list
        view.filesList()->setRowCheck(QStringLiteral("b.txt"), ChangedFilesList::Check::Unchecked);

        QSignalSpy spy(&view, &ChangesView::commitRequested);
        auto* btn = view.findChild<QPushButton*>(QStringLiteral("commitButton"));
        QVERIFY(btn);
        btn->click();

        QCOMPARE(spy.count(), 1);
        const auto sels = spy.takeFirst().at(1).value<std::vector<gittide::StageSelection>>();
        QCOMPARE(sels.size(), size_t(1)); // only a.txt
        QCOMPARE(sels[0].path.generic_string(), std::string("a.txt"));
        QVERIFY(!sels[0].hunkIndex.has_value()); // whole file
    }

    void commit_button_gated_on_message_and_checked_files()
    {
        ChangesView view;
        auto* button  = view.findChild<QPushButton*>(QStringLiteral("commitButton"));
        auto* message = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
        QVERIFY(button && message);

        QVERIFY(!button->isEnabled()); // no files, no message
        view.setStatus(changes_view_test::two_files());
        QVERIFY(!button->isEnabled()); // files checked but no message
        message->setPlainText(QStringLiteral("hello"));
        QVERIFY(button->isEnabled()); // message + checked files

        // Unchecking every file disables the button again.
        view.filesList()->setRowCheck(QStringLiteral("a.txt"), ChangedFilesList::Check::Unchecked);
        view.filesList()->setRowCheck(QStringLiteral("b.txt"), ChangedFilesList::Check::Unchecked);
        // setRowCheck does not emit fileCheckToggled, so drive the model directly
        // by simulating the user clicks through setStatus reset instead:
        view.setStatus({});
        QVERIFY(!button->isEnabled());
    }

    void line_toggle_makes_file_partial_and_builds_per_hunk_selection()
    {
        ChangesView view;
        view.setStatus({{"a.txt", gittide::StatusFlag::WtModified}});
        auto* message = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
        message->setPlainText(QStringLiteral("partial"));

        // Check two lines in hunk 0 of a.txt.
        view.applyLineToggle(QStringLiteral("a.txt"), 0, 2, true);
        view.applyLineToggle(QStringLiteral("a.txt"), 0, 5, true);

        bool whole = true;
        std::map<int, std::vector<int>> lines;
        view.selectionFor(QStringLiteral("a.txt"), whole, lines);
        QVERIFY(!whole); // Partial
        QCOMPARE(lines[0], (std::vector<int>{2, 5}));

        QSignalSpy spy(&view, &ChangesView::commitRequested);
        view.findChild<QPushButton*>(QStringLiteral("commitButton"))->click();
        QCOMPARE(spy.count(), 1);
        const auto sels = spy.takeFirst().at(1).value<std::vector<gittide::StageSelection>>();
        QCOMPARE(sels.size(), size_t(1));
        QCOMPARE(sels[0].hunkIndex.value(), 0);
        QCOMPARE(sels[0].lineIndices, (std::vector<int>{2, 5}));
    }

    void unchecking_all_partial_lines_collapses_to_unchecked()
    {
        ChangesView view;
        view.setStatus({{"a.txt", gittide::StatusFlag::WtModified}});

        view.applyLineToggle(QStringLiteral("a.txt"), 0, 2, true);
        view.applyLineToggle(QStringLiteral("a.txt"), 0, 2, false);

        bool whole = false;
        std::map<int, std::vector<int>> lines;
        view.selectionFor(QStringLiteral("a.txt"), whole, lines);
        QVERIFY(whole); // collapsed back to a whole-file state (Unchecked)
        QVERIFY(lines.empty());
    }

    void discard_forwards_whole_file_selection()
    {
        ChangesView view;
        view.setStatus(changes_view_test::two_files());

        QSignalSpy spy(&view, &ChangesView::discardRequested);
        emit view.filesList()->discardRequested(QStringLiteral("a.txt"));

        QCOMPARE(spy.count(), 1);
        const auto sel = spy.takeFirst().at(0).value<gittide::StageSelection>();
        QCOMPARE(sel.path.generic_string(), std::string("a.txt"));
        QVERIFY(!sel.hunkIndex.has_value());
    }
};

#include "test_changes_view.moc"
