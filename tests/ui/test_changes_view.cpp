#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

#include "gittide/ui/ChangesView.hpp"
#include "gittide/ui/Metatypes.hpp"

using gittide::ui::ChangesView;

namespace changes_view_test {
std::vector<gittide::FileStatus> mixed_status() {
    return {
        {std::filesystem::path("staged.txt"),   gittide::StatusFlag::IndexModified},
        {std::filesystem::path("unstaged.txt"), gittide::StatusFlag::WtModified},
        {std::filesystem::path("both.txt"),
            gittide::StatusFlag::IndexModified | gittide::StatusFlag::WtModified},
    };
}
}  // namespace changes_view_test

class TestChangesView : public QObject {
    Q_OBJECT
private slots:
    void splits_status_into_staged_and_unstaged() {
        ChangesView view;
        view.setStatus(changes_view_test::mixed_status());
        auto* staged = view.findChild<QListWidget*>(QStringLiteral("stagedList"));
        auto* unstaged = view.findChild<QListWidget*>(QStringLiteral("unstagedList"));
        QVERIFY(staged && unstaged);
        QCOMPARE(staged->count(), 2);    // staged.txt + both.txt
        QCOMPARE(unstaged->count(), 2);  // unstaged.txt + both.txt
    }

    void commit_button_gated_on_message_and_staged() {
        ChangesView view;
        auto* button = view.findChild<QPushButton*>(QStringLiteral("commitButton"));
        auto* message = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
        QVERIFY(button && message);

        view.setStatus(changes_view_test::mixed_status());
        QVERIFY(!button->isEnabled());          // staged but no message
        message->setPlainText(QStringLiteral("hello"));
        QVERIFY(button->isEnabled());           // message + staged
        view.setStatus({});                     // nothing staged
        QVERIFY(!button->isEnabled());
    }

    void selecting_unstaged_file_emits_worktree_target() {
        ChangesView view;
        view.setStatus(changes_view_test::mixed_status());
        auto* unstaged = view.findChild<QListWidget*>(QStringLiteral("unstagedList"));

        QSignalSpy spy(&view, &ChangesView::fileSelected);
        unstaged->setCurrentRow(0);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).value<gittide::DiffTarget>(),
                 gittide::DiffTarget::WorktreeVsIndex);
    }

    void commit_button_emits_request_with_message() {
        ChangesView view;
        view.setStatus(changes_view_test::mixed_status());
        auto* button = view.findChild<QPushButton*>(QStringLiteral("commitButton"));
        auto* message = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
        message->setPlainText(QStringLiteral("my commit"));

        QSignalSpy spy(&view, &ChangesView::commitRequested);
        button->click();

        QCOMPARE(spy.count(), 1);
        const auto req = spy.at(0).at(0).value<gittide::CommitRequest>();
        QCOMPARE(QString::fromStdString(req.message), QStringLiteral("my commit"));
    }
};

#include "test_changes_view.moc"
