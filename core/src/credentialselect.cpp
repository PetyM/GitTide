#include "gittide/sync.hpp"

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

CredentialKind chooseCredential(std::string_view url, unsigned allowedTypes, const Credentials& cred)
{
    if (isSshUrl(url) && cred.sshUseAgent && (allowedTypes & GIT_CREDENTIAL_SSH_KEY))
    {
        logf(LogLevel::Debug, logcat::AUTH, "selecting ssh-agent credential (allowed types {:#x})", allowedTypes);
        return CredentialKind::SshAgent;
    }
    if ((allowedTypes & GIT_CREDENTIAL_USERPASS_PLAINTEXT) && !cred.username.empty() && !cred.password.empty())
    {
        logf(LogLevel::Debug, logcat::AUTH, "selecting https userpass credential for user '{}'", cred.username);
        return CredentialKind::UserPass;
    }
    logf(LogLevel::Warning, logcat::AUTH, "no usable credential for remote (allowed types {:#x})", allowedTypes);
    return CredentialKind::None;
}

} // namespace gittide
