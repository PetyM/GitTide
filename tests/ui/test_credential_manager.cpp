#include <QRandomGenerator>
#include <QtTest/QtTest>
#include <filesystem>
#include <qcorotask.h>
#include <string>

#include "gittide/credentialsstore.hpp"
#include "gittide/gitrepo.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/ui/credentialmanager.hpp"
#include "gittide/ui/secretstore.hpp"
#include "support/temprepo.hpp"

using namespace gittide;
using gittide::ui::CredentialManager;
using gittide::ui::InMemorySecretStore;

namespace {
std::filesystem::path tempCredPath()
{
    return std::filesystem::temp_directory_path()
           / ("gittide_cm_" + std::to_string(::QRandomGenerator::global()->generate()) + ".json");
}

// The generic-UTF-8 form CredentialManager keys repo overrides by.
std::string genericOf(const std::filesystem::path& p)
{
    const auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}
} // namespace

class TestCredentialManager : public QObject
{
    Q_OBJECT
private slots:
    void applies_repo_override_to_local_config()
    {
        test::TempRepo   tmp;
        ProjectStore     projects;
        CredentialsStore creds;
        auto&            id = creds.addIdentity("Rob", "rob@x.com");
        creds.setRepoOverride(genericOf(tmp.path()), id.id);

        CredentialManager cm(&creds, tempCredPath(), &projects);
        QCoro::waitFor(cm.applyIdentityToRepo(QString::fromStdString(genericOf(tmp.path()))));

        auto repo = GitRepo::open(tmp.path());
        QVERIFY(repo.has_value());
        auto info = repo->localIdentity();
        QVERIFY(info.has_value());
        QCOMPARE(QString::fromStdString(info->email), QStringLiteral("rob@x.com"));
        QVERIFY(info->managed);
        QCOMPARE(QString::fromStdString(info->marker), QString::fromStdString(id.id));
    }

    void never_clobbers_cli_set_local_identity()
    {
        test::TempRepo tmp;
        tmp.setIdentity("CLI", "cli@x.com"); // unmarked local identity
        ProjectStore     projects;
        CredentialsStore creds;
        auto&            id = creds.addIdentity("Rob", "rob@x.com");
        creds.setRepoOverride(genericOf(tmp.path()), id.id);

        CredentialManager cm(&creds, tempCredPath(), &projects);
        QCoro::waitFor(cm.applyIdentityToRepo(QString::fromStdString(genericOf(tmp.path()))));

        auto info = GitRepo::open(tmp.path())->localIdentity();
        QVERIFY(info.has_value());
        QCOMPARE(QString::fromStdString(info->email), QStringLiteral("cli@x.com")); // untouched
        QVERIFY(!info->managed);
    }

    void clears_managed_local_when_assignment_removed()
    {
        test::TempRepo   tmp;
        ProjectStore     projects;
        CredentialsStore creds;
        auto&            id  = creds.addIdentity("Rob", "rob@x.com");
        const std::string key = genericOf(tmp.path());
        creds.setRepoOverride(key, id.id);

        CredentialManager cm(&creds, tempCredPath(), &projects);
        const QString     path = QString::fromStdString(key);
        QCoro::waitFor(cm.applyIdentityToRepo(path));
        QVERIFY(GitRepo::open(tmp.path())->localIdentity()->managed);

        // Remove the override → the next apply clears the managed local identity so
        // the repo falls back to the global-config identity.
        creds.clearRepoOverride(key);
        QCoro::waitFor(cm.applyIdentityToRepo(path));
        auto info = GitRepo::open(tmp.path())->localIdentity();
        QVERIFY(info.has_value());
        QVERIFY(!info->managed);
        QVERIFY(!info->hasEmail);
    }

    void credentials_for_https_uses_matched_host_token()
    {
        ProjectStore        projects;
        CredentialsStore    creds;
        InMemorySecretStore secrets;
        auto& h = creds.addHost("github.com", "github", "https://api.github.com", "octocat", "token", "");
        QCoro::waitFor(secrets.write(QStringLiteral("host-token:") + QString::fromStdString(h.id),
                                     QStringLiteral("ghp_xyz")));

        CredentialManager cm(&creds, tempCredPath(), &projects, &secrets);
        auto c = QCoro::waitFor(cm.credentialsForRemote(QStringLiteral("https://github.com/octocat/repo.git")));
        QCOMPARE(QString::fromStdString(c.username), QStringLiteral("octocat"));
        QCOMPARE(QString::fromStdString(c.password), QStringLiteral("ghp_xyz"));
    }

    void credentials_for_ssh_uses_keyfile_and_passphrase()
    {
        ProjectStore        projects;
        CredentialsStore    creds;
        InMemorySecretStore secrets;
        auto& k = creds.addSshKey("work", "/home/u/.ssh/id_ed25519.pub", "/home/u/.ssh/id_ed25519", true);
        QCoro::waitFor(secrets.write(QStringLiteral("ssh-passphrase:") + QString::fromStdString(k.id),
                                     QStringLiteral("pass")));

        CredentialManager cm(&creds, tempCredPath(), &projects, &secrets);
        auto c = QCoro::waitFor(cm.credentialsForRemote(QStringLiteral("git@github.com:octocat/repo.git")));
        QVERIFY(!c.sshUseAgent);
        QCOMPARE(QString::fromStdString(c.sshPrivateKeyPath), QStringLiteral("/home/u/.ssh/id_ed25519"));
        QCOMPARE(QString::fromStdString(c.sshPassphrase), QStringLiteral("pass"));
    }

    void credentials_for_ssh_falls_back_to_agent_without_keys()
    {
        ProjectStore        projects;
        CredentialsStore    creds;
        InMemorySecretStore secrets;
        CredentialManager   cm(&creds, tempCredPath(), &projects, &secrets);
        auto c = QCoro::waitFor(cm.credentialsForRemote(QStringLiteral("git@github.com:o/r.git")));
        QVERIFY(c.sshUseAgent);
    }

    void remember_host_token_persists_for_next_lookup()
    {
        ProjectStore        projects;
        CredentialsStore    creds;
        InMemorySecretStore secrets;
        CredentialManager   cm(&creds, tempCredPath(), &projects, &secrets);
        cm.rememberHostToken(QStringLiteral("https://gitlab.com/o/r.git"), QStringLiteral("me"),
                             QStringLiteral("tok"));
        auto c = QCoro::waitFor(cm.credentialsForRemote(QStringLiteral("https://gitlab.com/o/r.git")));
        QCOMPARE(QString::fromStdString(c.username), QStringLiteral("me"));
        QCOMPARE(QString::fromStdString(c.password), QStringLiteral("tok"));
    }
};

#include "test_credential_manager.moc"
