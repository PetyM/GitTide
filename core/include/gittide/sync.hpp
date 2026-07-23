#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gittide {

// Ahead/behind of the current branch versus its upstream remote-tracking ref.
struct SyncStatus
{
    bool        hasUpstream = false; // false when the current branch has no upstream
    int         ahead       = 0;     // local commits not on the upstream
    int         behind      = 0;     // upstream commits not present locally
    std::string upstreamName;        // e.g. "origin/main"; empty when no upstream
    std::string remoteName;          // e.g. "origin"; empty when no upstream
};

// How pull reconciles a diverged branch. Persisted in git config (pull.rebase).
enum class PullStrategy
{
    FastForwardOnly, // fast-forward if possible, else error
    Rebase,          // rebase local commits onto the upstream
};

// A single SSH private key on disk, offered by the credential callback. The
// public-key path and passphrase are optional (empty when unknown / not
// encrypted). Secret material (the passphrase) is filled from the OS keychain at
// call time and never persisted in this struct's lifetime beyond the operation.
struct SshKeyfile
{
    std::string privateKeyPath; // ssh keyfile: private key path
    std::string publicKeyPath;  // ssh keyfile: public key path (optional)
    std::string passphrase;     // ssh keyfile: passphrase (may be empty)
};

// Credentials supplied by the UI before a network call. Pure std; no Qt. For ssh
// remotes the callback tries the agent first (when sshUseAgent) then each keyfile
// in order — mirroring OpenSSH, which authenticates with the agent or a default
// identity file without either being configured up front.
struct Credentials
{
    bool                    sshUseAgent = true; // ssh remotes: try ssh-agent first
    std::string             username;           // https username (or ssh user override)
    std::string             password;           // https token / password
    std::vector<SshKeyfile> sshKeyfiles;        // ssh keyfiles, tried in order after the agent
};

// Which credential kind a callback should produce for a given remote URL.
enum class CredentialKind
{
    SshAgent,
    SshKey,
    UserPass,
    None,
};

// One authentication attempt in an ordered plan. keyIndex indexes into
// Credentials::sshKeyfiles and is meaningful only when kind == SshKey.
struct CredentialAttempt
{
    CredentialKind kind;
    std::size_t    keyIndex = 0;
};

// The ordered authentication attempts to make for `url`, given libgit2's
// allowed_types bitmask and the available credentials: ssh-agent first (when
// enabled), then each configured keyfile with a non-empty private path; or a
// single HTTPS userpass. Empty when nothing applies. Pure — unit-testable
// without a repository or a live remote. The credential callback walks this plan
// across libgit2's GIT_EAUTH retry loop, one attempt per invocation.
std::vector<CredentialAttempt> credentialAttempts(std::string_view url, unsigned allowedTypes, const Credentials& cred);

// The conventional OpenSSH default identity keyfiles present under `sshDir`
// (typically ~/.ssh), in OpenSSH's preference order. A key is included only if
// its private-key file exists; the public-key path is set to `<priv>.pub` when
// that sibling exists, else left empty. Passphrases are left empty (unknown here;
// an encrypted default key without the agent is a known limitation). Pure std;
// home resolution stays in the caller so core carries no HOME magic.
std::vector<SshKeyfile> discoverDefaultSshKeyfiles(const std::filesystem::path& sshDir);

} // namespace gittide
