#include "gittide/sync.hpp"

#include <array>
#include <system_error>

#include <git2.h>

#include "gittide/log.hpp"

namespace gittide {

namespace {
// SSH-style: explicit scheme, or scp-like "user@host:path" with no "://".
bool isSshUrl(std::string_view url)
{
    if (url.starts_with("ssh://"))
        return true;
    if (url.find("://") != std::string_view::npos)
        return false; // some other explicit scheme (https/git/file)
    auto at = url.find('@');
    auto colon = url.find(':');
    return at != std::string_view::npos && colon != std::string_view::npos && at < colon;
}
} // namespace

std::vector<CredentialAttempt> credentialAttempts(std::string_view url, unsigned allowedTypes, const Credentials& cred)
{
    std::vector<CredentialAttempt> plan;
    if (isSshUrl(url) && (allowedTypes & GIT_CREDENTIAL_SSH_KEY))
    {
        if (cred.sshUseAgent)
            plan.push_back({CredentialKind::SshAgent, 0});
        for (std::size_t i = 0; i < cred.sshKeyfiles.size(); ++i)
        {
            if (!cred.sshKeyfiles[i].privateKeyPath.empty())
                plan.push_back({CredentialKind::SshKey, i});
        }
        if (plan.empty())
            logf(LogLevel::Warning, logcat::AUTH, "no ssh credential available (allowed types {:#x})", allowedTypes);
        return plan;
    }
    if ((allowedTypes & GIT_CREDENTIAL_USERPASS_PLAINTEXT) && !cred.username.empty() && !cred.password.empty())
    {
        plan.push_back({CredentialKind::UserPass, 0});
        return plan;
    }
    logf(LogLevel::Warning, logcat::AUTH, "no usable credential for remote (allowed types {:#x})", allowedTypes);
    return plan;
}

std::vector<SshKeyfile> discoverDefaultSshKeyfiles(const std::filesystem::path& sshDir)
{
    // OpenSSH's default identity files, in the order ssh(1) tries them.
    static constexpr std::array kNames = {
        "id_ed25519", "id_ecdsa", "id_ecdsa_sk", "id_ed25519_sk", "id_rsa", "id_dsa",
    };
    std::vector<SshKeyfile> keys;
    std::error_code         ec;
    for (const char* name : kNames)
    {
        const std::filesystem::path priv = sshDir / name;
        if (!std::filesystem::is_regular_file(priv, ec))
            continue;
        const std::filesystem::path pub = sshDir / (std::string(name) + ".pub");
        SshKeyfile                  k;
        k.privateKeyPath = priv.generic_string();
        if (std::filesystem::is_regular_file(pub, ec))
            k.publicKeyPath = pub.generic_string();
        keys.push_back(std::move(k));
    }
    if (!keys.empty())
        logf(LogLevel::Debug, logcat::AUTH, "discovered {} default ssh identity file(s) under '{}'", keys.size(),
             sshDir.generic_string());
    return keys;
}

} // namespace gittide
