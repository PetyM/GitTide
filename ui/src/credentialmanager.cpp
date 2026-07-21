#include <algorithm>

#include <QVariantMap>

#include <gittide/credentialsstore.hpp>
#include <gittide/gitrepo.hpp>
#include <gittide/log.hpp>
#include <gittide/projectstore.hpp>
#include <gittide/ui/asyncrepo.hpp>
#include <gittide/ui/credentialmanager.hpp>
#include <gittide/ui/forgeclient.hpp>
#include <gittide/ui/hostlistmodel.hpp>
#include <gittide/ui/metatypes.hpp>
#include <gittide/ui/secretstore.hpp>
#include <gittide/ui/sshkeylistmodel.hpp>

namespace gittide::ui {

namespace {
// Normalize a QString path to the generic UTF-8 form RepoRef::path is stored in,
// so override keys and project-repo matches compare apples to apples.
std::string normPath(const QString& qs)
{
    const auto u8 = qstringToPath(qs).generic_u8string();
    return std::string(u8.begin(), u8.end());
}

// Host portion of a git remote URL: "https://github.com/o/r" → "github.com",
// "git@github.com:o/r" → "github.com". Empty if none can be parsed.
std::string hostOf(const std::string& url)
{
    std::string rest = url;
    if (auto pos = rest.find("://"); pos != std::string::npos)
        rest = rest.substr(pos + 3);
    if (auto at = rest.find('@'); at != std::string::npos)
        rest = rest.substr(at + 1);
    if (auto sep = rest.find_first_of("/:"); sep != std::string::npos)
        rest = rest.substr(0, sep);
    return rest;
}

bool isHttpsUrl(const std::string& url)
{
    return url.starts_with("https://") || url.starts_with("http://");
}

QString hostTokenKey(const std::string& id) { return QStringLiteral("host-token:") + QString::fromStdString(id); }
QString sshPassphraseKey(const std::string& id) { return QStringLiteral("ssh-passphrase:") + QString::fromStdString(id); }
} // namespace

CredentialManager::CredentialManager(gittide::CredentialsStore* store, std::filesystem::path storePath,
                                     gittide::ProjectStore* projects, SecretStore* secrets, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_storePath(std::move(storePath))
    , m_projects(projects)
    , m_secrets(secrets)
    , m_identities(new IdentityListModel(store, this))
    , m_hostModel(new HostListModel(store, this))
    , m_sshKeyModel(new SshKeyListModel(store, this))
    , m_forge(new ForgeClient(this))
{
    if (!m_secrets)
    {
        m_ownedSecrets = std::make_unique<KeychainSecretStore>();
        m_secrets      = m_ownedSecrets.get();
    }

    // First-run convenience: if we hold no identities yet but the user already
    // has a global git identity, adopt it as the Global identity so the Identity
    // tab is not empty. Guarded on emptiness ⇒ genuinely one-time (never
    // resurrects a deleted identity, never runs once the user has any).
    if (m_store->identities().empty())
    {
        if (auto gid = gittide::GitRepo::globalIdentity();
            gid.has_value() && !gid->name.empty() && !gid->email.empty())
        {
            const auto& id = m_store->addIdentity(gid->name, gid->email);
            setGlobalIdentity(QString::fromStdString(id.id));
        }
    }
}

CredentialManager::~CredentialManager() = default;

QString CredentialManager::globalIdentityId() const
{
    return QString::fromStdString(m_store->globalIdentity());
}

void CredentialManager::save()
{
    if (auto r = m_store->save(m_storePath); !r)
        logf(LogLevel::Warning, logcat::APP, "failed to save credentials.json: {}", r.error().message);
}

void CredentialManager::addIdentity(const QString& name, const QString& email)
{
    m_store->addIdentity(name.toStdString(), email.toStdString());
    save();
    m_identities->refresh();
    emit changed();
}

void CredentialManager::updateIdentity(const QString& id, const QString& name, const QString& email)
{
    m_store->updateIdentity(id.toStdString(), name.toStdString(), email.toStdString());
    save();
    m_identities->refresh();
    emit changed();
}

void CredentialManager::removeIdentity(const QString& id)
{
    m_store->removeIdentity(id.toStdString());
    save();
    m_identities->refresh();
    emit changed();
}

void CredentialManager::setGlobalIdentity(const QString& id)
{
    m_store->setGlobalIdentity(id.toStdString());
    save();
    // Materialize into ~/.gitconfig (static: no repo needed; libgit2 is initialised
    // for the app's lifetime by the process-wide LibGit2Context).
    if (const gittide::GitIdentity* ident = m_store->findIdentity(id.toStdString()))
    {
        if (auto r = gittide::GitRepo::setGlobalIdentity(ident->name, ident->email); !r)
            logf(LogLevel::Warning, logcat::AUTH, "failed to write global git identity: {}", r.error().message);
    }
    m_identities->refresh(); // isGlobal role changed
    emit changed();
}

void CredentialManager::setProjectDefault(const QString& projectId, const QString& identityId)
{
    m_store->setProjectDefault(projectId.toStdString(), identityId.toStdString());
    save();
    emit changed();
    // Re-materialize the active repo if it belongs to this project.
    if (!m_lastAppliedPath.isEmpty())
    {
        m_lastAppliedPath.clear(); // force re-apply
        QCoro::connect(applyIdentityToRepo(QString()), this, [] {});
    }
}

void CredentialManager::setRepoOverride(const QString& repoPath, const QString& identityId)
{
    m_store->setRepoOverride(normPath(repoPath), identityId.toStdString());
    save();
    emit changed();
    m_lastAppliedPath.clear(); // force re-apply
    QCoro::connect(applyIdentityToRepo(repoPath), this, [] {});
}

QString CredentialManager::repoOverrideId(const QString& repoPath) const
{
    return QString::fromStdString(m_store->repoOverride(normPath(repoPath)));
}

QString CredentialManager::projectDefaultId(const QString& projectId) const
{
    return QString::fromStdString(m_store->projectDefault(projectId.toStdString()));
}

QVariantList CredentialManager::identityChoices() const
{
    QVariantList out;
    for (const auto& id : m_store->identities())
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromStdString(id.id));
        m.insert(QStringLiteral("name"), QString::fromStdString(id.name));
        m.insert(QStringLiteral("email"), QString::fromStdString(id.email));
        out.append(m);
    }
    return out;
}

QString CredentialManager::inheritedIdentityId(const QString& projectId) const
{
    if (!projectId.isEmpty())
    {
        const std::string pd = m_store->projectDefault(projectId.toStdString());
        if (!pd.empty())
            return QString::fromStdString(pd);
    }
    return QString::fromStdString(m_store->globalIdentity());
}

std::vector<std::string> CredentialManager::candidateProjectIds(const std::string& repoPath) const
{
    std::vector<std::string> out;
    if (!m_projects)
        return out;
    for (const auto& p : m_projects->projects())
    {
        for (const auto& r : p.repos)
        {
            if (r.path == repoPath)
            {
                out.push_back(p.id);
                break;
            }
        }
    }
    // Active project first, so a repo shared across projects resolves against the
    // one the user is currently looking at.
    const std::string active = m_projects->activeProject();
    std::stable_partition(out.begin(), out.end(), [&](const std::string& id) { return id == active; });
    return out;
}

void CredentialManager::onActiveRepoChanged(const QString& repoPath)
{
    if (repoPath == m_lastAppliedPath)
        return; // deduplicate: RepoViewModel::changed fires for many reasons
    QCoro::connect(applyIdentityToRepo(repoPath), this, [] {});
}

QCoro::Task<void> CredentialManager::applyIdentityToRepo(QString repoPath)
{
    // An empty path means "re-apply the last active repo" (used after an assignment
    // change). Fall back to the remembered path.
    if (repoPath.isEmpty())
        repoPath = m_lastAppliedPath;
    if (repoPath.isEmpty())
        co_return;
    m_lastAppliedPath = repoPath;

    const std::string        key        = normPath(repoPath);
    const auto               candidates = candidateProjectIds(key);
    const auto               local      = m_store->resolveLocalIdentity(key, candidates);

    auto opened = AsyncRepo::open(qstringToPath(repoPath));
    if (!opened)
        co_return;
    AsyncRepo repo = std::move(*opened);

    auto infoR = co_await repo.localIdentity();
    if (!infoR)
        co_return;
    const gittide::LocalIdentityInfo info = *infoR;

    // Never clobber a CLI-set (unmarked) local identity — leave it as the user set it.
    if ((info.hasName || info.hasEmail) && !info.managed)
        co_return;

    if (local)
    {
        co_await repo.setLocalIdentity(QString::fromStdString(local->name),
                                       QString::fromStdString(local->email),
                                       QString::fromStdString(local->id));
    }
    else if (info.managed)
    {
        // We managed it before, but there is no longer an override/project default
        // → clear so the repo falls back to the global-config identity.
        co_await repo.clearLocalIdentity();
    }
    co_return;
}

void CredentialManager::addHost(const QString& host, const QString& kind, const QString& apiBase,
                                const QString& username, const QString& token)
{
    auto& h = m_store->addHost(host.toStdString(), kind.toStdString(), apiBase.toStdString(), username.toStdString(),
                               "token", std::string{});
    save();
    if (!token.isEmpty())
        QCoro::connect(m_secrets->write(hostTokenKey(h.id), token), this, [](bool) {});
    m_hostModel->refresh();
    emit changed();
}

void CredentialManager::removeHost(const QString& id)
{
    QCoro::connect(m_secrets->remove(hostTokenKey(id.toStdString())), this, [](bool) {});
    m_store->removeHost(id.toStdString());
    save();
    m_hostModel->refresh();
    emit changed();
}

void CredentialManager::validateAndAddHost(const QString& host, const QString& kind, const QString& apiBase,
                                           const QString& token)
{
    QCoro::connect(runValidateAndAddHost(host, kind, apiBase, token), this, [] {});
}

QCoro::Task<void> CredentialManager::runValidateAndAddHost(QString host, QString kind, QString apiBase, QString token)
{
    const ForgeAccount acc = co_await m_forge->validate(kind, apiBase, token);
    if (!acc.ok)
    {
        emit hostValidated(false, acc.error);
        co_return;
    }

    addHost(host, kind, apiBase, acc.login, token); // persists token + refreshes model

    // Create a matching identity if the forge exposed an email and none exists yet.
    if (!acc.email.isEmpty())
    {
        const std::string email = acc.email.toStdString();
        bool              have  = false;
        for (const auto& i : m_store->identities())
            if (i.email == email)
                have = true;
        if (!have)
        {
            m_store->addIdentity(acc.name.isEmpty() ? acc.login.toStdString() : acc.name.toStdString(), email);
            save();
            m_identities->refresh();
        }
    }
    emit changed();
    emit hostValidated(true, QStringLiteral("Signed in as %1").arg(acc.login));
}

void CredentialManager::addSshKey(const QString& label, const QString& publicKeyPath, const QString& privateKeyPath,
                                  const QString& passphrase)
{
    auto& k = m_store->addSshKey(label.toStdString(), publicKeyPath.toStdString(), privateKeyPath.toStdString(),
                                 !passphrase.isEmpty());
    save();
    if (!passphrase.isEmpty())
        QCoro::connect(m_secrets->write(sshPassphraseKey(k.id), passphrase), this, [](bool) {});
    m_sshKeyModel->refresh();
    emit changed();
}

void CredentialManager::removeSshKey(const QString& id)
{
    QCoro::connect(m_secrets->remove(sshPassphraseKey(id.toStdString())), this, [](bool) {});
    m_store->removeSshKey(id.toStdString());
    save();
    m_sshKeyModel->refresh();
    emit changed();
}

void CredentialManager::rememberHostToken(const QString& url, const QString& username, const QString& token)
{
    const std::string host = hostOf(url.toStdString());
    if (host.empty() || token.isEmpty())
        return;

    // Reuse an existing account for this host, else create one.
    std::string id;
    for (const auto& h : m_store->hosts())
    {
        if (h.host == host)
        {
            id = h.id;
            break;
        }
    }
    if (id.empty())
    {
        auto& h = m_store->addHost(host, "generic", std::string{}, username.toStdString(), "token", std::string{});
        id      = h.id;
        save();
        m_hostModel->refresh();
        emit changed();
    }
    QCoro::connect(m_secrets->write(hostTokenKey(id), token), this, [](bool) {});
}

QCoro::Task<gittide::Credentials> CredentialManager::credentialsForRemote(QString url)
{
    gittide::Credentials cred;
    const std::string    u = url.toStdString();

    if (isHttpsUrl(u))
    {
        // Match a stored host account by hostname; supply its username + keychain token.
        const std::string host = hostOf(u);
        for (const auto& h : m_store->hosts())
        {
            if (h.host == host)
            {
                cred.username = h.username;
                cred.password = (co_await m_secrets->read(hostTokenKey(h.id))).toStdString();
                break;
            }
        }
        cred.sshUseAgent = false; // irrelevant for https, but avoids an agent probe
        co_return cred;
    }

    // SSH: use the first configured keyfile (+ passphrase from the keychain); with
    // no keyfile configured, fall back to the ssh-agent.
    if (!m_store->sshKeys().empty())
    {
        const auto& k         = m_store->sshKeys().front();
        cred.sshUseAgent      = false;
        cred.sshPublicKeyPath = k.publicKeyPath;
        cred.sshPrivateKeyPath = k.privateKeyPath;
        if (k.hasPassphrase)
            cred.sshPassphrase = (co_await m_secrets->read(sshPassphraseKey(k.id))).toStdString();
    }
    else
    {
        cred.sshUseAgent = true;
    }
    co_return cred;
}

} // namespace gittide::ui
