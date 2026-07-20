#pragma once
#include <string>
#include <string_view>

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

// Credentials supplied by the UI before a network call. Pure std; no Qt. Secret
// fields (password, sshPassphrase) are filled from the OS keychain at call time
// and never persisted in this struct's lifetime beyond the operation.
struct Credentials
{
    bool        sshUseAgent = true; // ssh remotes: authenticate via ssh-agent
    std::string username;           // https username (or ssh user override)
    std::string password;           // https token / password
    std::string sshPublicKeyPath;   // ssh keyfile: public key path (optional)
    std::string sshPrivateKeyPath;  // ssh keyfile: private key path
    std::string sshPassphrase;      // ssh keyfile: passphrase (may be empty)
};

// Which credential kind a callback should produce for a given remote URL.
enum class CredentialKind
{
    SshAgent,
    SshKey,
    UserPass,
    None,
};

// Pure selection logic used by the libgit2 credential callback. allowedTypes is
// a git_credential_t bitmask. Kept free-standing so it is unit-testable without
// a repository or a live remote.
CredentialKind chooseCredential(std::string_view url, unsigned allowedTypes, const Credentials& cred);

} // namespace gittide
