#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include <filesystem>
#include <fstream>

#include "gittide/sync.hpp"

using gittide::credentialAttempts;
using gittide::CredentialKind;
using gittide::Credentials;
using gittide::discoverDefaultSshKeyfiles;
using gittide::SshKeyfile;

TEST_CASE("credentialAttempts offers ssh-agent for ssh urls", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = true;
    auto plan     = credentialAttempts("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c);
    REQUIRE(plan.size() == 1);
    REQUIRE(plan[0].kind == CredentialKind::SshAgent);
    REQUIRE(credentialAttempts("ssh://git@host/repo.git", GIT_CREDENTIAL_SSH_KEY, c)[0].kind == CredentialKind::SshAgent);
}

TEST_CASE("credentialAttempts offers userpass for https with a token", "[sync][cred]")
{
    Credentials c;
    c.username = "me";
    c.password = "ghp_token";
    auto plan  = credentialAttempts("https://github.com/me/repo.git", GIT_CREDENTIAL_USERPASS_PLAINTEXT, c);
    REQUIRE(plan.size() == 1);
    REQUIRE(plan[0].kind == CredentialKind::UserPass);
}

TEST_CASE("credentialAttempts is empty when https has no token", "[sync][cred]")
{
    Credentials c; // empty username/password
    REQUIRE(credentialAttempts("https://github.com/me/repo.git", GIT_CREDENTIAL_USERPASS_PLAINTEXT, c).empty());
}

TEST_CASE("credentialAttempts is empty for ssh with agent off and no keyfile", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = false; // no private key configured either
    REQUIRE(credentialAttempts("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c).empty());
}

TEST_CASE("credentialAttempts offers the ssh keyfile when agent is off and a key is set", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = false;
    c.sshKeyfiles.push_back({"/home/u/.ssh/id_ed25519", "/home/u/.ssh/id_ed25519.pub", "s3cret"});
    auto plan = credentialAttempts("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c);
    REQUIRE(plan.size() == 1);
    REQUIRE(plan[0].kind == CredentialKind::SshKey);
    REQUIRE(plan[0].keyIndex == 0);
}

TEST_CASE("credentialAttempts tries the agent first, then each keyfile in order", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = true;
    c.sshKeyfiles.push_back({"/home/u/.ssh/id_ed25519", "", ""});
    c.sshKeyfiles.push_back({"/home/u/.ssh/id_rsa", "", ""});
    auto plan = credentialAttempts("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c);
    REQUIRE(plan.size() == 3);
    REQUIRE(plan[0].kind == CredentialKind::SshAgent);
    REQUIRE(plan[1].kind == CredentialKind::SshKey);
    REQUIRE(plan[1].keyIndex == 0);
    REQUIRE(plan[2].kind == CredentialKind::SshKey);
    REQUIRE(plan[2].keyIndex == 1);
}

TEST_CASE("credentialAttempts skips keyfiles with an empty private path", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = false;
    c.sshKeyfiles.push_back({"", "", ""}); // malformed entry is ignored
    c.sshKeyfiles.push_back({"/home/u/.ssh/id_ed25519", "", ""});
    auto plan = credentialAttempts("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c);
    REQUIRE(plan.size() == 1);
    REQUIRE(plan[0].keyIndex == 1);
}

TEST_CASE("discoverDefaultSshKeyfiles finds conventional keys in preference order", "[sync][cred]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "gittide_ssh_disc";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto touch = [&](const char* name)
    {
        std::ofstream(dir / name) << "x";
    };
    // Present out of order; id_rsa has no .pub sibling, id_ed25519 does.
    touch("id_rsa");
    touch("id_ed25519");
    touch("id_ed25519.pub");
    touch("config"); // must be ignored

    auto keys = discoverDefaultSshKeyfiles(dir);
    REQUIRE(keys.size() == 2);
    REQUIRE(keys[0].privateKeyPath == (dir / "id_ed25519").generic_string()); // ed25519 preferred
    REQUIRE(keys[0].publicKeyPath == (dir / "id_ed25519.pub").generic_string());
    REQUIRE(keys[1].privateKeyPath == (dir / "id_rsa").generic_string());
    REQUIRE(keys[1].publicKeyPath.empty()); // no sibling .pub
    REQUIRE(keys[1].passphrase.empty());

    fs::remove_all(dir);
}

TEST_CASE("discoverDefaultSshKeyfiles returns nothing for a missing or empty dir", "[sync][cred]")
{
    namespace fs = std::filesystem;
    REQUIRE(discoverDefaultSshKeyfiles(fs::temp_directory_path() / "gittide_no_such_ssh_dir").empty());
}
