#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include <filesystem>
#include <random>
#include <string>

#include "gittide/gitrepo.hpp"
#include "gittide/pathutil.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;

TEST_CASE("setLocalIdentity writes local user.* and the ownership marker", "[identity]")
{
    gittide::test::TempRepo tmp;
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    REQUIRE(repo->setLocalIdentity("Jane Dev", "jane@work.com", "id-123").has_value());

    // git_signature_default (what commit uses) now reads it from the local config.
    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gittide::toGitPath(tmp.path()).c_str()) == 0);
    git_signature* sig = nullptr;
    REQUIRE(git_signature_default(&sig, r) == 0);
    REQUIRE(std::string(sig->name) == "Jane Dev");
    REQUIRE(std::string(sig->email) == "jane@work.com");
    git_signature_free(sig);
    git_repository_free(r);

    auto info = repo->localIdentity();
    REQUIRE(info.has_value());
    REQUIRE(info->hasName);
    REQUIRE(info->hasEmail);
    REQUIRE(info->name == "Jane Dev");
    REQUIRE(info->email == "jane@work.com");
    REQUIRE(info->managed);
    REQUIRE(info->marker == "id-123");
}

TEST_CASE("a commit after setLocalIdentity records the materialized author", "[identity]")
{
    gittide::test::TempRepo tmp;
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->setLocalIdentity("Rob Repo", "rob@repo.com", "id-x").has_value());

    tmp.writeFile("a.txt", "hi\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    auto oid = repo->commit(gittide::CommitRequest{"first"});
    REQUIRE(oid.has_value());

    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gittide::toGitPath(tmp.path()).c_str()) == 0);
    git_oid head;
    REQUIRE(git_reference_name_to_id(&head, r, "HEAD") == 0);
    git_commit* c = nullptr;
    REQUIRE(git_commit_lookup(&c, r, &head) == 0);
    REQUIRE(std::string(git_commit_author(c)->name) == "Rob Repo");
    REQUIRE(std::string(git_commit_author(c)->email) == "rob@repo.com");
    git_commit_free(c);
    git_repository_free(r);
}

TEST_CASE("clearLocalIdentity removes the local identity and marker", "[identity]")
{
    gittide::test::TempRepo tmp;
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->setLocalIdentity("Local", "local@x.com", "id-1").has_value());
    REQUIRE(repo->localIdentity()->hasEmail);

    REQUIRE(repo->clearLocalIdentity().has_value());
    auto info = repo->localIdentity();
    REQUIRE(info.has_value());
    REQUIRE_FALSE(info->hasName);
    REQUIRE_FALSE(info->hasEmail);
    REQUIRE_FALSE(info->managed);

    // Clearing an already-clean repo is a no-op success (ENOTFOUND tolerated).
    REQUIRE(repo->clearLocalIdentity().has_value());
}

TEST_CASE("localIdentity reports CLI-set local config as unmanaged", "[identity]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("CLI User", "cli@x.com"); // writes local user.* WITHOUT our marker

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto info = repo->localIdentity();
    REQUIRE(info.has_value());
    REQUIRE(info->hasEmail);
    REQUIRE(info->email == "cli@x.com");
    REQUIRE_FALSE(info->managed); // no gittide.identity marker → hands off
    REQUIRE(info->marker.empty());
}

TEST_CASE("setGlobalIdentity writes the global config, creating it if absent", "[identity]")
{
    namespace fs = std::filesystem;
    // The core suite blanks the GLOBAL search path for isolation; point it at a
    // fresh temp dir so this test exercises the real global-config write path
    // (including creating the file) without touching the developer's ~/.gitconfig.
    fs::path gdir = fs::temp_directory_path() / ("gittide_global_" + std::to_string(std::random_device{}()));
    fs::create_directories(gdir);
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, gdir.string().c_str());

    {
        gittide::test::TempRepo tmp;
        auto repo = GitRepo::open(tmp.path());
        REQUIRE(repo.has_value());
        REQUIRE(repo->setGlobalIdentity("Global Gal", "global@x.com").has_value());

        REQUIRE(fs::exists(gdir / ".gitconfig"));

        // A repo with no local identity resolves the global one via merged config.
        auto eff = repo->effectiveIdentity();
        REQUIRE(eff.has_value());
        REQUIRE(eff->name == "Global Gal");
        REQUIRE(eff->email == "global@x.com");
    }

    // Restore isolation for subsequent tests.
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, "");
    fs::remove_all(gdir);
}
