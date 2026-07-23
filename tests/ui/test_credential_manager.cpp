#include <QRandomGenerator>
#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>
#include <git2.h>
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
        QVERIFY(!c.sshUseAgent); // an explicitly configured key is honoured; no agent probe
        QCOMPARE(c.sshKeyfiles.size(), size_t{1});
        QCOMPARE(QString::fromStdString(c.sshKeyfiles[0].privateKeyPath), QStringLiteral("/home/u/.ssh/id_ed25519"));
        QCOMPARE(QString::fromStdString(c.sshKeyfiles[0].passphrase), QStringLiteral("pass"));
    }

    // No in-app key and an empty ~/.ssh ⇒ the agent is the only attempt.
    void credentials_for_ssh_falls_back_to_agent_without_keys()
    {
        ProjectStore        projects;
        CredentialsStore    creds;
        InMemorySecretStore secrets;
        CredentialManager   cm(&creds, tempCredPath(), &projects, &secrets);

        const auto emptyDir = std::filesystem::temp_directory_path()
                              / ("gittide_ssh_empty_" + std::to_string(::QRandomGenerator::global()->generate()));
        std::filesystem::create_directories(emptyDir);
        cm.setDefaultSshDir(emptyDir);

        auto c = QCoro::waitFor(cm.credentialsForRemote(QStringLiteral("git@github.com:o/r.git")));
        QVERIFY(c.sshUseAgent);
        QVERIFY(c.sshKeyfiles.empty());
        std::filesystem::remove_all(emptyDir);
    }

    // No in-app key but a default identity file on disk ⇒ agent first, then the
    // discovered key — the CLI-parity path this plan adds.
    void credentials_for_ssh_discovers_default_identity_file()
    {
        ProjectStore        projects;
        CredentialsStore    creds;
        InMemorySecretStore secrets;
        CredentialManager   cm(&creds, tempCredPath(), &projects, &secrets);

        const auto dir = std::filesystem::temp_directory_path()
                         / ("gittide_ssh_def_" + std::to_string(::QRandomGenerator::global()->generate()));
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "id_ed25519") << "x";
        cm.setDefaultSshDir(dir);

        auto c = QCoro::waitFor(cm.credentialsForRemote(QStringLiteral("git@github.com:o/r.git")));
        QVERIFY(c.sshUseAgent);
        QCOMPARE(c.sshKeyfiles.size(), size_t{1});
        QCOMPARE(QString::fromStdString(c.sshKeyfiles[0].privateKeyPath),
                 QString::fromStdString((dir / "id_ed25519").generic_string()));
        std::filesystem::remove_all(dir);
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

    void identity_choices_lists_all_identities()
    {
        ProjectStore     projects;
        CredentialsStore creds;
        auto& a = creds.addIdentity("Alice", "alice@x.com");
        auto& b = creds.addIdentity("Bob", "bob@x.com");

        CredentialManager cm(&creds, tempCredPath(), &projects);
        const QVariantList rows = cm.identityChoices();

        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value("id").toString(), QString::fromStdString(a.id));
        QCOMPARE(rows.at(0).toMap().value("name").toString(), QStringLiteral("Alice"));
        QCOMPARE(rows.at(0).toMap().value("email").toString(), QStringLiteral("alice@x.com"));
        QCOMPARE(rows.at(1).toMap().value("id").toString(), QString::fromStdString(b.id));
    }

    void inherited_identity_prefers_project_default_then_global()
    {
        ProjectStore     projects;
        CredentialsStore creds;
        auto& g = creds.addIdentity("Global Gwen", "gwen@x.com");
        auto& p = creds.addIdentity("Project Pat", "pat@x.com");
        creds.setGlobalIdentity(g.id);
        creds.setProjectDefault("proj-1", p.id);

        CredentialManager cm(&creds, tempCredPath(), &projects);

        // Project with a default → its default id.
        QCOMPARE(cm.inheritedIdentityId(QStringLiteral("proj-1")), QString::fromStdString(p.id));
        // Project without a default → the global id.
        QCOMPARE(cm.inheritedIdentityId(QStringLiteral("proj-none")), QString::fromStdString(g.id));
        // Empty project id → the global id.
        QCOMPARE(cm.inheritedIdentityId(QString()), QString::fromStdString(g.id));
    }

    void seeds_identity_from_global_config_when_store_empty()
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path()
            / ("gittide_seed_" + std::to_string(::QRandomGenerator::global()->generate()));
        fs::create_directories(dir);
        { std::ofstream cfg(dir / ".gitconfig");
          cfg << "[user]\n\tname = Seed Sam\n\temail = sam@seed.com\n"; }
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL,
                         dir.generic_string().c_str());

        ProjectStore     projects;
        CredentialsStore creds; // empty
        CredentialManager cm(&creds, tempCredPath(), &projects);

        QCOMPARE(int(creds.identities().size()), 1);
        QCOMPARE(QString::fromStdString(creds.identities().front().name), QStringLiteral("Seed Sam"));
        QCOMPARE(cm.globalIdentityId(),
                 QString::fromStdString(creds.identities().front().id));

        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, "");
        fs::remove_all(dir);
    }

    void does_not_seed_when_store_already_has_identity()
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path()
            / ("gittide_noseed_" + std::to_string(::QRandomGenerator::global()->generate()));
        fs::create_directories(dir);
        { std::ofstream cfg(dir / ".gitconfig");
          cfg << "[user]\n\tname = Seed Sam\n\temail = sam@seed.com\n"; }
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL,
                         dir.generic_string().c_str());

        ProjectStore     projects;
        CredentialsStore creds;
        creds.addIdentity("Existing Ed", "ed@x.com"); // store not empty
        CredentialManager cm(&creds, tempCredPath(), &projects);

        QCOMPARE(int(creds.identities().size()), 1); // no extra seeded identity
        QCOMPARE(QString::fromStdString(creds.identities().front().name), QStringLiteral("Existing Ed"));

        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, "");
        fs::remove_all(dir);
    }
};

#include "test_credential_manager.moc"
