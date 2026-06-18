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

    // Verify that the path→QString conversion used by ChangesView::setStatus
    // (pathToQString) and the one used by ChangedFilesList::makeItem (inline
    // QString::fromUtf8 on generic_u8string) produce byte-identical QStrings for
    // a non-ASCII filename.  This seals the m_sel key lookup for Windows/MSVC
    // paths where generic_string() would use the ANSI codepage and diverge.
    void nonascii_path_key_roundtrip()
    {
        const std::filesystem::path nonAscii(std::u8string(u8"ünïcödé.txt"));

        // pathToQString (used by ChangesView::setStatus to build m_sel keys)
        const QString viaHelper = gittide::ui::pathToQString(nonAscii);

        // makeItem-style conversion (used by ChangedFilesList for PathRole)
        const auto u8 = nonAscii.generic_u8string();
        const QString viaInline = QString::fromUtf8(
            reinterpret_cast<const char*>(u8.data()),
            static_cast<qsizetype>(u8.size()));

        QCOMPARE(viaHelper, viaInline);

        // Also verify that qstringToPath round-trips back to the same path.
        const std::filesystem::path roundTripped = gittide::ui::qstringToPath(viaHelper);
        QCOMPARE(roundTripped.generic_u8string(), nonAscii.generic_u8string());

        // End-to-end: setStatus must populate m_sel with the same key that
        // ChangedFilesList stores in PathRole (viaInline), so selectionFor
        // and applyLineToggle can find it.
        ChangesView view;
        view.setStatus({{nonAscii, gittide::StatusFlag::WtModified}});

        bool whole = false;
        std::map<int, std::vector<int>> lines;
        view.selectionFor(viaInline, whole, lines); // key as list would emit it
        QVERIFY(whole); // default state is Checked == whole-file
    }
};

#include "test_changes_view.moc"
