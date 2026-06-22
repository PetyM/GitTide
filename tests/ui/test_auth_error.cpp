#include <QtTest/QtTest>

#include "gittide/giterror.hpp"
#include "gittide/ui/autherror.hpp"

using gittide::GitError;
using gittide::ui::isAuthError;

class TestAuthError : public QObject
{
    Q_OBJECT
private slots:
    void eauth_code_is_auth() { QVERIFY(isAuthError(GitError{.code = -16, .message = "x"})); }
    void message_authentication_is_auth() { QVERIFY(isAuthError(GitError{.code = -1, .message = "remote authentication required"})); }
    void message_401_is_auth() { QVERIFY(isAuthError(GitError{.code = -1, .message = "unexpected http status code: 401"})); }
    void other_error_is_not_auth() { QVERIFY(!isAuthError(GitError{.code = -3, .message = "reference not found"})); }
};

#include "test_auth_error.moc"
