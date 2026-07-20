#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>

#include "gittide/credentialsstore.hpp"

using gittide::CredentialsStore;
using gittide::GitIdentity;
using gittide::HostAccount;
using gittide::SshKeyRef;

namespace {
std::filesystem::path temp_json_path()
{
    std::random_device rd;
    return std::filesystem::temp_directory_path() / ("gittide_creds_" + std::to_string(rd()) + ".json");
}
} // namespace

TEST_CASE("CredentialsStore round-trips identities, ssh keys, hosts and assignments", "[creds]")
{
    CredentialsStore store;
    auto& id = store.addIdentity("Jane Dev", "jane@work.com");
    store.setGlobalIdentity(id.id);
    store.addSshKey("work-ed25519", "/home/u/.ssh/id_ed25519.pub", "/home/u/.ssh/id_ed25519", true);
    store.addHost("github.com", "github", "https://api.github.com", "janedev", "token", id.id);
    store.setProjectDefault("proj-1", id.id);
    store.setRepoOverride("/home/u/api", id.id);

    auto loaded = CredentialsStore::from_json(store.to_json());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->identities().size() == 1);
    REQUIRE(loaded->identities()[0].name == "Jane Dev");
    REQUIRE(loaded->identities()[0].email == "jane@work.com");
    REQUIRE(loaded->globalIdentity() == id.id);
    REQUIRE(loaded->sshKeys().size() == 1);
    REQUIRE(loaded->sshKeys()[0].label == "work-ed25519");
    REQUIRE(loaded->sshKeys()[0].hasPassphrase);
    REQUIRE(loaded->hosts().size() == 1);
    REQUIRE(loaded->hosts()[0].host == "github.com");
    REQUIRE(loaded->hosts()[0].identityId == id.id);
    REQUIRE(loaded->projectDefault("proj-1") == id.id);
    REQUIRE(loaded->repoOverride("/home/u/api") == id.id);
}

TEST_CASE("empty CredentialsStore round-trips empty with default version", "[creds]")
{
    CredentialsStore store;
    auto loaded = CredentialsStore::from_json(store.to_json());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->identities().empty());
    REQUIRE(loaded->globalIdentity().empty());
    REQUIRE(loaded->loadedVersion() == CredentialsStore::kVersion);
}

TEST_CASE("CredentialsStore never persists a secret field", "[creds]")
{
    CredentialsStore store;
    auto& id = store.addIdentity("Jane", "jane@x.com");
    store.addHost("github.com", "github", "https://api.github.com", "janedev", "token", id.id);
    store.addSshKey("k", "/p.pub", "/p", true);

    const std::string json = store.to_json();
    // A metadata-only store must never carry token / password / passphrase keys.
    REQUIRE(json.find("token") != std::string::npos);   // authType == "token" is fine
    REQUIRE(json.find("password") == std::string::npos);
    REQUIRE(json.find("passphrase") == std::string::npos);
    REQUIRE(json.find("secret") == std::string::npos);
}

TEST_CASE("malformed CredentialsStore JSON returns an error, never throws", "[creds]")
{
    auto bad = CredentialsStore::from_json("{ not json");
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().code != 0);

    auto wrong = CredentialsStore::from_json(R"({"identities": "oops"})");
    REQUIRE_FALSE(wrong.has_value());

    auto notObj = CredentialsStore::from_json("[1,2,3]");
    REQUIRE_FALSE(notObj.has_value());
}

TEST_CASE("CredentialsStore save/load round-trips through disk", "[creds]")
{
    auto path = temp_json_path();
    CredentialsStore store;
    store.addIdentity("Jane", "jane@x.com");
    REQUIRE(store.save(path).has_value());

    auto loaded = CredentialsStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->identities().size() == 1);

    std::filesystem::remove(path);
}

TEST_CASE("CredentialsStore load of a missing file returns an empty store", "[creds]")
{
    auto loaded = CredentialsStore::load(temp_json_path());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->identities().empty());
}

TEST_CASE("CredentialsStore load of corrupt JSON backs up the file and returns empty", "[creds]")
{
    auto path = temp_json_path();
    {
        std::ofstream(path) << "{ this is not json";
    }
    auto loaded = CredentialsStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->identities().empty());
    REQUIRE(std::filesystem::exists(path.string() + ".corrupt"));
    REQUIRE_FALSE(std::filesystem::exists(path));

    std::filesystem::remove(path.string() + ".corrupt");
}

TEST_CASE("addIdentity assigns a unique non-empty id", "[creds][mutations]")
{
    CredentialsStore store;
    auto& a = store.addIdentity("A", "a@x.com");
    auto& b = store.addIdentity("B", "b@x.com");
    REQUIRE_FALSE(a.id.empty());
    REQUIRE_FALSE(b.id.empty());
    REQUIRE(a.id != b.id);
}

TEST_CASE("removeIdentity clears it from global and all assignments", "[creds][mutations]")
{
    CredentialsStore store;
    auto& id = store.addIdentity("Jane", "jane@x.com");
    const std::string idId = id.id;
    store.setGlobalIdentity(idId);
    store.setProjectDefault("proj-1", idId);
    store.setRepoOverride("/home/u/api", idId);

    store.removeIdentity(idId);
    REQUIRE(store.identities().empty());
    REQUIRE(store.globalIdentity().empty());
    REQUIRE(store.projectDefault("proj-1").empty());
    REQUIRE(store.repoOverride("/home/u/api").empty());
}

TEST_CASE("resolveIdentity: repo override wins over project default and global", "[creds][resolve]")
{
    CredentialsStore store;
    auto& g = store.addIdentity("Global", "g@x.com");
    auto& p = store.addIdentity("Project", "p@x.com");
    auto& r = store.addIdentity("Repo", "r@x.com");
    store.setGlobalIdentity(g.id);
    store.setProjectDefault("proj-1", p.id);
    store.setRepoOverride("/home/u/api", r.id);

    const std::array<std::string, 1> cands{"proj-1"};
    auto got = store.resolveIdentity("/home/u/api", cands);
    REQUIRE(got.has_value());
    REQUIRE(got->email == "r@x.com");
}

TEST_CASE("resolveIdentity: project default wins over global when no repo override", "[creds][resolve]")
{
    CredentialsStore store;
    auto& g = store.addIdentity("Global", "g@x.com");
    auto& p = store.addIdentity("Project", "p@x.com");
    store.setGlobalIdentity(g.id);
    store.setProjectDefault("proj-1", p.id);

    const std::array<std::string, 1> cands{"proj-1"};
    auto got = store.resolveIdentity("/home/u/api", cands);
    REQUIRE(got.has_value());
    REQUIRE(got->email == "p@x.com");
}

TEST_CASE("resolveIdentity: falls back to global, then to nullopt", "[creds][resolve]")
{
    CredentialsStore store;
    auto& g = store.addIdentity("Global", "g@x.com");
    store.setGlobalIdentity(g.id);

    const std::array<std::string, 1> cands{"proj-1"};
    auto got = store.resolveIdentity("/home/u/api", cands);
    REQUIRE(got.has_value());
    REQUIRE(got->email == "g@x.com");

    CredentialsStore empty;
    REQUIRE_FALSE(empty.resolveIdentity("/home/u/api", cands).has_value());
}

TEST_CASE("resolveLocalIdentity excludes the global default (that lives in global config)", "[creds][resolve]")
{
    CredentialsStore store;
    auto& g = store.addIdentity("Global", "g@x.com");
    auto& p = store.addIdentity("Project", "p@x.com");
    store.setGlobalIdentity(g.id);

    const std::array<std::string, 1> cands{"proj-1"};

    // No repo override, no project default → nothing to materialize locally, even
    // though the full resolveIdentity would report the global identity for display.
    REQUIRE_FALSE(store.resolveLocalIdentity("/home/u/api", cands).has_value());
    REQUIRE(store.resolveIdentity("/home/u/api", cands)->email == "g@x.com");

    // A project default is materialized locally (overriding global config).
    store.setProjectDefault("proj-1", p.id);
    REQUIRE(store.resolveLocalIdentity("/home/u/api", cands)->email == "p@x.com");
}

TEST_CASE("resolveIdentity: multi-project fallback is deterministic in candidate order", "[creds][resolve]")
{
    CredentialsStore store;
    auto& first  = store.addIdentity("First", "first@x.com");
    auto& second = store.addIdentity("Second", "second@x.com");
    store.setProjectDefault("proj-A", first.id);
    store.setProjectDefault("proj-B", second.id);

    // The repo is in both projects; the first candidate (active project) wins.
    const std::array<std::string, 2> candsAB{"proj-A", "proj-B"};
    REQUIRE(store.resolveIdentity("/home/u/api", candsAB)->email == "first@x.com");

    const std::array<std::string, 2> candsBA{"proj-B", "proj-A"};
    REQUIRE(store.resolveIdentity("/home/u/api", candsBA)->email == "second@x.com");
}
