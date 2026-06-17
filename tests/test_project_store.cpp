#include <catch2/catch_test_macros.hpp>

#include "gittide/projectstore.hpp"

TEST_CASE("ProjectStore serializes and deserializes round-trip", "[store]")
{
    gittide::ProjectStore store;
    gittide::Project p;
    p.id   = "uuid-1";
    p.name = "Work";
    p.repos.push_back(gittide::RepoRef{"/home/u/api", "api"});
    p.lastActiveRepo = "/home/u/api";
    store.projects().push_back(p);
    store.setActiveProject("uuid-1");

    std::string json = store.to_json();
    auto loaded      = gittide::ProjectStore::from_json(json);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->activeProject() == "uuid-1");
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "Work");
    REQUIRE(loaded->projects()[0].repos.size() == 1);
    REQUIRE(loaded->projects()[0].repos[0].alias == "api");
}

TEST_CASE("empty store round-trips to an empty store", "[store]")
{
    gittide::ProjectStore store;
    auto loaded = gittide::ProjectStore::from_json(store.to_json());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
    REQUIRE(loaded->activeProject().empty());
    REQUIRE(loaded->loadedVersion() == gittide::ProjectStore::kVersion);
}

TEST_CASE("malformed JSON returns an error, never throws", "[store]")
{
    auto loaded = gittide::ProjectStore::from_json("{ this is not json");
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(loaded.error().code != 0);
}

TEST_CASE("wrong-typed \"projects\" key degrades to an error, never throws", "[store]")
{
    // "projects" present but not an array — must not throw past Expected.
    auto a = gittide::ProjectStore::from_json(R"({"projects": null})");
    REQUIRE_FALSE(a.has_value());

    auto b = gittide::ProjectStore::from_json(R"({"projects": "oops"})");
    REQUIRE_FALSE(b.has_value());
}

TEST_CASE("missing optional keys degrade to defaults", "[store]")
{
    // A project object missing every field is tolerated with empty defaults.
    auto loaded = gittide::ProjectStore::from_json(R"({"projects": [ {} ]})");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].id.empty());
    REQUIRE(loaded->projects()[0].repos.empty());
}

TEST_CASE("non-object root is rejected", "[store]")
{
    auto loaded = gittide::ProjectStore::from_json("[1, 2, 3]");
    REQUIRE_FALSE(loaded.has_value());
}

#include <filesystem>
#include <fstream>
#include <random>

namespace {
std::filesystem::path temp_json_path()
{
    std::random_device rd;
    return std::filesystem::temp_directory_path() / ("gittide_store_" + std::to_string(rd()) + ".json");
}
} // namespace

TEST_CASE("save then load round-trips through disk", "[store]")
{
    auto path = temp_json_path();
    gittide::ProjectStore store;
    gittide::Project p;
    p.id   = "x";
    p.name = "Proj";
    store.projects().push_back(p);

    auto saved = store.save(path);
    REQUIRE(saved.has_value());

    auto loaded = gittide::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "Proj");

    std::filesystem::remove(path);
}

TEST_CASE("load of a missing file returns an empty store", "[store]")
{
    auto loaded = gittide::ProjectStore::load(temp_json_path());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
}

TEST_CASE("load of corrupt JSON backs up the file and returns empty store", "[store]")
{
    auto path = temp_json_path();
    {
        std::ofstream(path) << "{ this is not json";
    }

    auto loaded = gittide::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
    REQUIRE(std::filesystem::exists(path.string() + ".corrupt"));
    REQUIRE_FALSE(std::filesystem::exists(path)); // original was renamed away

    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".corrupt");
}

TEST_CASE("createProject appends a project with unique id and given name", "[store][mutations]")
{
    gittide::ProjectStore store;
    auto& p1 = store.createProject("Alpha");
    auto& p2 = store.createProject("Beta");

    REQUIRE(store.projects().size() == 2);
    REQUIRE(p1.name == "Alpha");
    REQUIRE(p2.name == "Beta");
    REQUIRE(!p1.id.empty());
    REQUIRE(!p2.id.empty());
    REQUIRE(p1.id != p2.id);
}

TEST_CASE("createProject persists via save/load round-trip", "[store][mutations]")
{
    auto path = temp_json_path();
    gittide::ProjectStore store;
    store.createProject("MyProject");
    REQUIRE(store.save(path).has_value());

    auto loaded = gittide::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "MyProject");
    REQUIRE(!loaded->projects()[0].id.empty());

    std::filesystem::remove(path);
}

TEST_CASE("addRepo inserts a repo into the project", "[store][mutations]")
{
    gittide::ProjectStore store;
    auto& p     = store.createProject("Work");
    auto result = store.addRepo(p.id, gittide::RepoRef{"/home/u/api", "api"});
    REQUIRE(result.has_value());
    REQUIRE(store.projects()[0].repos.size() == 1);
    REQUIRE(store.projects()[0].repos[0].path == "/home/u/api");
    REQUIRE(store.projects()[0].repos[0].alias == "api");
}

TEST_CASE("addRepo rejects a duplicate path within the same project", "[store][mutations]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addRepo(p.id, gittide::RepoRef{"/home/u/api", "api"}).has_value());

    auto dup = store.addRepo(p.id, gittide::RepoRef{"/home/u/api", "api-copy"});
    REQUIRE_FALSE(dup.has_value());
    REQUIRE(!dup.error().message.empty());
    REQUIRE(store.projects()[0].repos.size() == 1);
}

TEST_CASE("addRepo returns error for unknown project id", "[store][mutations]")
{
    gittide::ProjectStore store;
    auto result = store.addRepo("no-such-id", gittide::RepoRef{"/some/path", "r"});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("addRepo round-trips through save/load", "[store][mutations]")
{
    auto path = temp_json_path();
    gittide::ProjectStore store;
    auto& p = store.createProject("Proj");
    store.addRepo(p.id, gittide::RepoRef{"/srv/myrepo", "myrepo"});
    REQUIRE(store.save(path).has_value());

    auto loaded = gittide::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects()[0].repos.size() == 1);
    REQUIRE(loaded->projects()[0].repos[0].alias == "myrepo");

    std::filesystem::remove(path);
}
