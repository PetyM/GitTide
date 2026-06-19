#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModel>

#include "gittide/ui/changedfilesmodel.hpp"
#include "gittide/filestatus.hpp"

using gittide::ui::ChangedFilesModel;
using gittide::FileStatus;
using gittide::StatusFlag;

namespace
{
int roleKey(const ChangedFilesModel& m, const QByteArray& name)
{
    const auto roles = m.roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}
}

class TestChangedFilesModel : public QObject
{
    Q_OBJECT
private slots:
    void maps_flags_to_letter_and_kind()
    {
        ChangedFilesModel m;
        std::vector<FileStatus> files;
        files.push_back({std::filesystem::path("src/a.cpp"), StatusFlag::IndexNew});
        files.push_back({std::filesystem::path("b.txt"), StatusFlag::WtModified});
        files.push_back({std::filesystem::path("c.txt"), StatusFlag::WtNew});
        m.setFiles(files);

        QCOMPARE(m.rowCount(QModelIndex()), 3);

        const int letter = roleKey(m, "statusLetter");
        const int kind   = roleKey(m, "statusKind");
        const int dir    = roleKey(m, "fileDir");
        const int name   = roleKey(m, "fileName");

        QCOMPARE(m.data(m.index(0, 0), letter).toString(), QStringLiteral("A"));
        QCOMPARE(m.data(m.index(0, 0), kind).toString(), QStringLiteral("added"));
        QCOMPARE(m.data(m.index(0, 0), dir).toString(), QStringLiteral("src/"));
        QCOMPARE(m.data(m.index(0, 0), name).toString(), QStringLiteral("a.cpp"));

        QCOMPARE(m.data(m.index(1, 0), letter).toString(), QStringLiteral("M"));
        QCOMPARE(m.data(m.index(1, 0), kind).toString(), QStringLiteral("modified"));
        QCOMPARE(m.data(m.index(1, 0), dir).toString(), QString());

        QCOMPARE(m.data(m.index(2, 0), letter).toString(), QStringLiteral("U"));
        QCOMPARE(m.data(m.index(2, 0), kind).toString(), QStringLiteral("untracked"));
    }

    void files_default_to_checked()
    {
        ChangedFilesModel m;
        m.setFiles({{std::filesystem::path("a"), StatusFlag::WtModified}});
        QCOMPARE(m.checkState(0), ChangedFilesModel::Checked);
        QCOMPARE(m.checkedCount(), 1);
    }

    void toggling_a_file_emits_datachanged_and_updates_state()
    {
        ChangedFilesModel m;
        m.setFiles({{std::filesystem::path("a"), StatusFlag::WtModified}});
        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);

        m.setChecked(0, false);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.checkState(0), ChangedFilesModel::Unchecked);
        QCOMPARE(m.checkedCount(), 0);
    }

    void path_lookup_round_trips()
    {
        ChangedFilesModel m;
        m.setFiles({{std::filesystem::path("src/a.cpp"), StatusFlag::WtModified}});
        QCOMPARE(m.pathAt(0), QStringLiteral("src/a.cpp"));
        QCOMPARE(m.rowForPath(QStringLiteral("src/a.cpp")), 0);
        QCOMPARE(m.rowForPath(QStringLiteral("nope")), -1);
    }
};

#include "test_changed_files_model.moc"
