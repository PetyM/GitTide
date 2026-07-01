#include <QtTest>

#include "gittide/ui/stashlistmodel.hpp"
#include "gittide/gitrepo.hpp"

using gittide::ui::StashListModel;
using gittide::StashEntry;

namespace
{
int roleKey(const StashListModel& m, const QByteArray& name)
{
    const auto roles = m.roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}
} // namespace

class TestStashListModel : public QObject
{
    Q_OBJECT
private slots:
    void empty_model_has_zero_rows()
    {
        StashListModel m;
        QCOMPARE(m.rowCount(QModelIndex()), 0);
        QCOMPARE(m.count(), 0);
    }

    void set_entries_populates_rows()
    {
        StashListModel m;
        std::vector<StashEntry> entries;
        entries.push_back({0, "WIP on main: abc", "aabbcc"});
        entries.push_back({1, "On feature: xyz",  "ddeeff"});
        m.setEntries(entries);

        QCOMPARE(m.rowCount(QModelIndex()), 2);
        QCOMPARE(m.count(), 2);
    }

    void label_role_formats_stash_at_index()
    {
        StashListModel m;
        m.setEntries({{0, "WIP on main: abc", "aabbcc"}});

        const int labelRole = roleKey(m, "label");
        QVERIFY(labelRole != -1);
        QCOMPARE(m.data(m.index(0, 0), labelRole).toString(),
                 QStringLiteral("stash@{0}"));
    }

    void message_role_returns_message()
    {
        StashListModel m;
        m.setEntries({{0, "WIP on main: abc", "aabbcc"}});

        const int messageRole = roleKey(m, "message");
        QVERIFY(messageRole != -1);
        QCOMPARE(m.data(m.index(0, 0), messageRole).toString(),
                 QStringLiteral("WIP on main: abc"));
    }

    void oid_role_returns_oid()
    {
        StashListModel m;
        m.setEntries({{0, "WIP on main: abc", "aabbcc"}});

        const int oidRole = roleKey(m, "oid");
        QVERIFY(oidRole != -1);
        QCOMPARE(m.data(m.index(0, 0), oidRole).toString(),
                 QStringLiteral("aabbcc"));
    }

    void oid_at_in_range_returns_oid()
    {
        StashListModel m;
        m.setEntries({{0, "WIP on main: abc", "aabbcc"},
                      {1, "On feature: xyz",  "ddeeff"}});

        QCOMPARE(m.oidAt(0), QStringLiteral("aabbcc"));
        QCOMPARE(m.oidAt(1), QStringLiteral("ddeeff"));
    }

    void oid_at_out_of_range_returns_empty()
    {
        StashListModel m;
        m.setEntries({{0, "WIP on main: abc", "aabbcc"}});

        QCOMPARE(m.oidAt(-1), QString());
        QCOMPARE(m.oidAt(1), QString());
    }

    void set_entries_resets_model()
    {
        StashListModel m;
        m.setEntries({{0, "first", "oid1"}});
        QCOMPARE(m.rowCount(QModelIndex()), 1);

        m.setEntries({});
        QCOMPARE(m.rowCount(QModelIndex()), 0);
        QCOMPARE(m.count(), 0);
    }

    void data_returns_invalid_for_bad_index()
    {
        StashListModel m;
        m.setEntries({{0, "WIP", "abc"}});

        const int labelRole = roleKey(m, "label");
        QVERIFY(!m.data(m.index(-1, 0), labelRole).isValid());
        QVERIFY(!m.data(m.index(1, 0), labelRole).isValid());
    }

    void multiple_entries_label_includes_index()
    {
        StashListModel m;
        m.setEntries({{0, "newest", "aaa"},
                      {1, "older",  "bbb"},
                      {2, "oldest", "ccc"}});

        const int labelRole = roleKey(m, "label");
        QCOMPARE(m.data(m.index(0, 0), labelRole).toString(), QStringLiteral("stash@{0}"));
        QCOMPARE(m.data(m.index(1, 0), labelRole).toString(), QStringLiteral("stash@{1}"));
        QCOMPARE(m.data(m.index(2, 0), labelRole).toString(), QStringLiteral("stash@{2}"));
    }
};

#include "test_stash_list_model.moc"
