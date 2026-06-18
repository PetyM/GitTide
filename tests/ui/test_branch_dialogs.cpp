// tests/ui/test_branch_dialogs.cpp
// Tests for branch dialog validation predicates.
// The exec()-based ask* wrappers are thin and not tested headlessly;
// only the pure validation predicate is covered here.

#include "gittide/ui/branchdialogs.hpp"
#include <QtTest>

using gittide::ui::isValidBranchNameInput;

class TestBranchDialogs : public QObject
{
    Q_OBJECT
private slots:
    // --- isValidBranchNameInput ---

    void empty_name_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("")));
    }

    void whitespace_only_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral(" ")));
        QVERIFY(!isValidBranchNameInput(QStringLiteral("\t")));
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a b")));
        QVERIFY(!isValidBranchNameInput(QStringLiteral("my branch")));
    }

    void tilde_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a~b")));
        QVERIFY(!isValidBranchNameInput(QStringLiteral("~main")));
    }

    void caret_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a^b")));
    }

    void colon_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a:b")));
    }

    void question_mark_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a?b")));
    }

    void asterisk_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a*b")));
    }

    void open_bracket_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a[b")));
    }

    void backslash_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("a\\b")));
    }

    void leading_dash_is_invalid()
    {
        QVERIFY(!isValidBranchNameInput(QStringLiteral("-main")));
        QVERIFY(!isValidBranchNameInput(QStringLiteral("--no-ff")));
    }

    void valid_simple_names_are_accepted()
    {
        QVERIFY(isValidBranchNameInput(QStringLiteral("main")));
        QVERIFY(isValidBranchNameInput(QStringLiteral("feat/my-feature")));
        QVERIFY(isValidBranchNameInput(QStringLiteral("fix-123")));
        QVERIFY(isValidBranchNameInput(QStringLiteral("release/1.0.0")));
        QVERIFY(isValidBranchNameInput(QStringLiteral("chore/update-deps")));
    }

    void internal_dash_is_accepted()
    {
        QVERIFY(isValidBranchNameInput(QStringLiteral("a-b")));
        QVERIFY(isValidBranchNameInput(QStringLiteral("feature-new")));
    }

    void slash_separated_names_are_accepted()
    {
        QVERIFY(isValidBranchNameInput(QStringLiteral("user/topic")));
        QVERIFY(isValidBranchNameInput(QStringLiteral("hotfix/1.2.3")));
    }
};
#include "test_branch_dialogs.moc"
