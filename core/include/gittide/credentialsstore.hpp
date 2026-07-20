#pragma once
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gittide/giterror.hpp"

namespace gittide {

/// A named git identity (author/committer name + email). Metadata only — no secret.
struct GitIdentity
{
    std::string id;
    std::string name;
    std::string email;
};

/// A reference to an on-disk SSH key pair. The passphrase (if any) lives in the OS
/// keychain, never here; `hasPassphrase` only records whether one should be fetched.
struct SshKeyRef
{
    std::string id;
    std::string label;
    std::string publicKeyPath;
    std::string privateKeyPath;
    bool        hasPassphrase = false;
};

/// A forge/host account used for HTTPS git auth (and, later, API validation). The
/// token lives in the OS keychain keyed by this account's id, never in JSON.
struct HostAccount
{
    std::string id;
    std::string host;       // e.g. "github.com"
    std::string kind;       // "github" | "gitlab" | "generic"
    std::string apiBase;    // e.g. "https://api.github.com"
    std::string username;   // login used for HTTPS basic/token auth
    std::string authType;   // "token" for now
    std::string identityId; // optional link to a GitIdentity
};

/// In-memory model of `credentials.json` — the non-secret metadata half of
/// credentials management (identities, SSH-key/host references, and the
/// global/project/repo identity assignments). Secrets (HTTPS tokens, SSH
/// passphrases) are deliberately absent: they live only in the OS keychain,
/// looked up by host/key id from `ui/`.
///
/// Mirrors ProjectStore: versioned, atomic save (temp+rename), corrupt-file
/// recovery on load. Pure `std`; no Qt, no libgit2.
class CredentialsStore
{
public:
    static constexpr int kVersion = 1;

    // --- Collections (mutable + const views). std::deque (like ProjectStore) so
    //     a reference returned by add*() stays valid across later insertions. ---
    std::deque<GitIdentity>&       identities()       { return m_identities; }
    const std::deque<GitIdentity>& identities() const { return m_identities; }
    std::deque<SshKeyRef>&         sshKeys()          { return m_sshKeys; }
    const std::deque<SshKeyRef>&   sshKeys() const    { return m_sshKeys; }
    std::deque<HostAccount>&       hosts()            { return m_hosts; }
    const std::deque<HostAccount>& hosts() const      { return m_hosts; }

    // --- Global identity ---
    const std::string& globalIdentity() const { return m_globalIdentity; }
    void               setGlobalIdentity(std::string identityId) { m_globalIdentity = std::move(identityId); }

    // --- Assignments (empty string == "not assigned") ---
    std::string projectDefault(const std::string& projectId) const;
    void        setProjectDefault(const std::string& projectId, std::string identityId);
    void        clearProjectDefault(const std::string& projectId);

    std::string repoOverride(const std::string& repoPath) const;
    void        setRepoOverride(const std::string& repoPath, std::string identityId);
    void        clearRepoOverride(const std::string& repoPath);

    const std::map<std::string, std::string>& projectDefaults() const { return m_projectDefaults; }
    const std::map<std::string, std::string>& repoOverrides() const   { return m_repoOverrides; }

    /// Schema version read from the parsed document (kVersion for an in-memory
    /// store), so a future migration step can detect an older on-disk schema.
    int loadedVersion() const { return m_loadedVersion; }

    // --- Mutations (call save() afterwards to persist) ---

    /// Append a new identity with a random unique id; returns a reference to it.
    GitIdentity& addIdentity(const std::string& name, const std::string& email);
    /// Update name/email of the identity with this id (no-op if not found).
    void updateIdentity(const std::string& id, const std::string& name, const std::string& email);
    /// Remove an identity and scrub it from the global/project/repo assignments.
    void removeIdentity(const std::string& id);

    SshKeyRef& addSshKey(const std::string& label, const std::string& publicKeyPath,
                         const std::string& privateKeyPath, bool hasPassphrase);
    void       removeSshKey(const std::string& id);

    HostAccount& addHost(const std::string& host, const std::string& kind, const std::string& apiBase,
                         const std::string& username, const std::string& authType, const std::string& identityId);
    void         removeHost(const std::string& id);

    /// Look up an identity by id; nullptr if absent.
    const GitIdentity* findIdentity(const std::string& id) const;

    /// The identity to materialize into a repo's LOCAL config: repo override, else
    /// the first project default in `candidateProjectIdsInPriorityOrder`, else
    /// nullopt. The global identity is intentionally NOT considered — it lives in
    /// the global git config, so a repo with no override/project default should
    /// clear its local identity and fall back to global, not copy it down.
    std::optional<GitIdentity> resolveLocalIdentity(std::string_view repoPath,
                                                    std::span<const std::string> candidateProjectIdsInPriorityOrder) const;

    /// The effective identity for display ("this repo commits as X"): the local
    /// resolution above, else the global identity, else nullopt. Pure — the caller
    /// (ui) supplies the priority order from ProjectStore, so this stays free of
    /// any ProjectStore coupling.
    std::optional<GitIdentity> resolveIdentity(std::string_view repoPath,
                                               std::span<const std::string> candidateProjectIdsInPriorityOrder) const;

    // --- Serialization / persistence (mirrors ProjectStore) ---
    std::string                        to_json() const;
    static Expected<CredentialsStore>  from_json(const std::string& json);
    Expected<void>                     save(const std::filesystem::path& file) const;
    static Expected<CredentialsStore>  load(const std::filesystem::path& file);

private:
    std::deque<GitIdentity> m_identities;
    std::deque<SshKeyRef>   m_sshKeys;
    std::deque<HostAccount> m_hosts;
    std::string             m_globalIdentity;
    std::map<std::string, std::string> m_projectDefaults; // projectId -> identityId
    std::map<std::string, std::string> m_repoOverrides;   // repoPath  -> identityId
    int                      m_loadedVersion = kVersion;
};

} // namespace gittide
