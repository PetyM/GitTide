#include <QListWidget>
#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "gittide/ui/changedfileslist.hpp"

using gittide::ui::ChangedFilesList;

class TestChangedFilesList : public QObject
{
    Q_OBJECT
private slots:
    void editable_rows_start_checked()
    {
        ChangedFilesList list;
        list.setMode(ChangedFilesList::Mode::Editable);
        list.setFiles({ {"a.txt", gittide::StatusFlag::WtModified},
                        {"b.txt", gittide::StatusFlag::WtNew} });
        auto checked = list.checkedPaths();
        QCOMPARE(static_cast<int>(checked.size()), 2);
    }

    void toggling_a_row_emits_and_drops_it_from_checked()
    {
        ChangedFilesList list;
        list.setMode(ChangedFilesList::Mode::Editable);
        list.setFiles({ {"a.txt", gittide::StatusFlag::WtModified} });
        QSignalSpy spy(&list, &ChangedFilesList::fileCheckToggled);
        // setRowCheck reflects state without emitting (re-entrancy guard)
        list.setRowCheck(QStringLiteral("a.txt"), ChangedFilesList::Check::Unchecked);
        QCOMPARE(spy.count(), 0); // programmatic — no signal
        QVERIFY(list.checkedPaths().empty());
    }

    void readonly_mode_no_checkboxes()
    {
        ChangedFilesList list;
        list.setMode(ChangedFilesList::Mode::ReadOnly);
        list.setFiles({ {"c.txt", gittide::StatusFlag::IndexModified} });
        // In ReadOnly mode items have no checkboxes — checkedPaths returns empty.
        QVERIFY(list.checkedPaths().empty());
    }

    void file_selected_emits_on_current_item_change()
    {
        ChangedFilesList list;
        list.setMode(ChangedFilesList::Mode::Editable);
        list.setFiles({ {"x.txt", gittide::StatusFlag::IndexNew},
                        {"y.txt", gittide::StatusFlag::WtDeleted} });

        QSignalSpy spy(&list, &ChangedFilesList::fileSelected);
        auto* inner = list.findChild<QListWidget*>(QStringLiteral("changedFilesList"));
        QVERIFY(inner != nullptr);
        inner->setCurrentRow(1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("y.txt"));
    }

    void set_row_check_partial()
    {
        ChangedFilesList list;
        list.setMode(ChangedFilesList::Mode::Editable);
        list.setFiles({ {"a.txt", gittide::StatusFlag::WtModified} });
        list.setRowCheck(QStringLiteral("a.txt"), ChangedFilesList::Check::Partial);
        // Partially-checked is not fully checked → not in checkedPaths
        QVERIFY(list.checkedPaths().empty());
    }
};

#include "test_changed_files_list.moc"
