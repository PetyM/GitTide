#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "gittide/sync.hpp"

using gittide::chooseCredential;
using gittide::CredentialKind;
using gittide::Credentials;

TEST_CASE("chooseCredential picks ssh-agent for ssh urls", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = true;
    REQUIRE(chooseCredential("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::SshAgent);
    REQUIRE(chooseCredential("ssh://git@host/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::SshAgent);
}

TEST_CASE("chooseCredential picks userpass for https with a token", "[sync][cred]")
{
    Credentials c;
    c.username = "me";
    c.password = "ghp_token";
    REQUIRE(chooseCredential("https://github.com/me/repo.git", GIT_CREDENTIAL_USERPASS_PLAINTEXT, c) == CredentialKind::UserPass);
}

TEST_CASE("chooseCredential returns None when https has no token", "[sync][cred]")
{
    Credentials c; // empty username/password
    REQUIRE(chooseCredential("https://github.com/me/repo.git", GIT_CREDENTIAL_USERPASS_PLAINTEXT, c) == CredentialKind::None);
}

TEST_CASE("chooseCredential returns None for ssh with agent off and no keyfile", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = false; // no private key configured either
    REQUIRE(chooseCredential("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::None);
}

TEST_CASE("chooseCredential picks the ssh keyfile when agent is off and a key is set", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent       = false;
    c.sshPrivateKeyPath = "/home/u/.ssh/id_ed25519";
    c.sshPublicKeyPath  = "/home/u/.ssh/id_ed25519.pub";
    c.sshPassphrase     = "s3cret";
    REQUIRE(chooseCredential("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::SshKey);
    REQUIRE(chooseCredential("ssh://git@host/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::SshKey);
}

TEST_CASE("chooseCredential prefers ssh-agent over a configured keyfile", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent       = true; // agent wins when enabled
    c.sshPrivateKeyPath = "/home/u/.ssh/id_ed25519";
    REQUIRE(chooseCredential("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::SshAgent);
}
