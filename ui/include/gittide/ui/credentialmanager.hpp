#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <qcorotask.h>

#include "gittide/sync.hpp" // gittide::Credentials
#include "gittide/ui/identitylistmodel.hpp"

namespace gittide {
class CredentialsStore;
class ProjectStore;
} // namespace gittide

namespace gittide::ui {

class SecretStore;
class ForgeClient;
class HostListModel;
class SshKeyListModel;

/// Owns the credentials metadata store (`credentials.json`) and drives identity
/// management from QML: CRUD over named identities, the global/project/repo
/// assignments, and materializing the resolved identity into a repo's git config
/// (marker-guarded so CLI-set config is never clobbered — see D49).
///
/// This is the ui-side home of credentials; it references (does not own) the
/// ProjectStore so it can compute the candidate-project priority order a repo
/// resolves through. Secrets are NOT handled here — that arrives in Phase 2
/// (keychain-backed SecretStore).
class CredentialManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(gittide::ui::IdentityListModel* identities READ identities CONSTANT)
    Q_PROPERTY(QString globalIdentityId READ globalIdentityId NOTIFY changed)

public:
    /// `secrets` may be null → a default keychain-backed store is created and owned.
    /// Tests inject an InMemorySecretStore.
    CredentialManager(gittide::CredentialsStore* store, std::filesystem::path storePath,
                      gittide::ProjectStore* projects, SecretStore* secrets = nullptr, QObject* parent = nullptr);
    ~CredentialManager() override;

    IdentityListModel* identities() const { return m_identities; }
    HostListModel*     hosts() const { return m_hostModel; }
    SshKeyListModel*   sshKeys() const { return m_sshKeyModel; }
    QString            globalIdentityId() const;

    // --- CRUD from QML ---
    Q_INVOKABLE void addIdentity(const QString& name, const QString& email);
    Q_INVOKABLE void updateIdentity(const QString& id, const QString& name, const QString& email);
    Q_INVOKABLE void removeIdentity(const QString& id);

    /// Pick the global identity and write it to ~/.gitconfig (empty id just clears
    /// the GitTide-side pointer; it does not scrub the global config).
    Q_INVOKABLE void setGlobalIdentity(const QString& id);

    /// Assign a project's default / a repo's override identity (empty id clears),
    /// then re-materialize any affected open repo.
    Q_INVOKABLE void setProjectDefault(const QString& projectId, const QString& identityId);
    Q_INVOKABLE void setRepoOverride(const QString& repoPath, const QString& identityId);

    // --- Display helpers for the settings UI ---
    Q_INVOKABLE QString repoOverrideId(const QString& repoPath) const;
    Q_INVOKABLE QString projectDefaultId(const QString& projectId) const;

    // --- Host accounts (HTTPS/forge tokens). The token goes to the keychain. ---
    Q_INVOKABLE void addHost(const QString& host, const QString& kind, const QString& apiBase,
                             const QString& username, const QString& token);
    Q_INVOKABLE void removeHost(const QString& id);

    /// Validate a token against the forge API, then (on success) add the host with
    /// the returned login/email and create a matching identity if none exists.
    /// Emits hostValidated(ok, message) when done.
    Q_INVOKABLE void validateAndAddHost(const QString& host, const QString& kind, const QString& apiBase,
                                        const QString& token);

    // --- SSH keys. The passphrase (if any) goes to the keychain. ---
    Q_INVOKABLE void addSshKey(const QString& label, const QString& publicKeyPath, const QString& privateKeyPath,
                               const QString& passphrase);
    Q_INVOKABLE void removeSshKey(const QString& id);

    /// Persist a token entered in the fallback CredentialDialog: associate it with
    /// the remote URL's host (creating the host account if new) and store the token
    /// in the keychain, so the next session authenticates without re-prompting.
    Q_INVOKABLE void rememberHostToken(const QString& url, const QString& username, const QString& token);

    /// Assemble the `Credentials` POD for a remote URL from the stored metadata and
    /// the keychain: ssh URL → configured keyfile (+ passphrase) or ssh-agent;
    /// https URL → the matched host account's username + token. Used by the sync
    /// flow before dispatching a network op.
    QCoro::Task<gittide::Credentials> credentialsForRemote(QString url);

    /// Resolve + write the effective LOCAL identity into the repo's git config,
    /// unless the repo carries a CLI-set (unmarked) local identity. Idempotent.
    QCoro::Task<void> applyIdentityToRepo(QString repoPath);

public slots:
    /// React to the active repo changing (RepoViewModel::repoPath). De-duplicated
    /// so it only applies when the path actually changes.
    void onActiveRepoChanged(const QString& repoPath);

signals:
    void changed();
    /// Result of validateAndAddHost: ok true → host added; false → message is the error.
    void hostValidated(bool ok, const QString& message);

private:
    // Candidate project ids a repo resolves through, active project first.
    std::vector<std::string>          candidateProjectIds(const std::string& repoPath) const;
    void                              save();
    QCoro::Task<void>                 runValidateAndAddHost(QString host, QString kind, QString apiBase, QString token);

    gittide::CredentialsStore*   m_store;
    std::filesystem::path        m_storePath;
    gittide::ProjectStore*       m_projects;
    SecretStore*                 m_secrets;      // non-owning (may point at m_ownedSecrets)
    std::unique_ptr<SecretStore> m_ownedSecrets; // default keychain store, if we created one
    IdentityListModel*           m_identities;
    HostListModel*               m_hostModel;
    SshKeyListModel*             m_sshKeyModel;
    ForgeClient*                 m_forge;
    QString                      m_lastAppliedPath;
};

} // namespace gittide::ui
