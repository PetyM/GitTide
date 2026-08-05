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

TEST_CASE("sources round-trip through JSON", "[store][sources]")
{
    gittide::ProjectStore store;
    gittide::Project p;
    p.id   = "uuid-1";
    p.name = "Work";
    p.sources.push_back(gittide::RepoSource{.path = "/home/u/projects", .maxDepth = 3, .ignored = {"/home/u/projects/scratch"}});
    store.projects().push_back(p);

    auto loaded = gittide::ProjectStore::from_json(store.to_json());

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects()[0].sources.size() == 1);
    REQUIRE(loaded->projects()[0].sources[0].path == "/home/u/projects");
    REQUIRE(loaded->projects()[0].sources[0].maxDepth == 3);
    REQUIRE(loaded->projects()[0].sources[0].ignored.size() == 1);
    REQUIRE(loaded->projects()[0].sources[0].ignored[0] == "/home/u/projects/scratch");
}

TEST_CASE("a document without \"sources\" loads with an empty source list", "[store][sources]")
{
    // The pre-sources on-disk schema: still version 1, no migration needed.
    const std::string legacy = R"({
      "version": 1,
      "activeProject": "uuid-1",
      "projects": [ { "id": "uuid-1", "name": "Work", "repos": [ { "path": "/home/u/api", "alias": "" } ] } ]
    })";

    auto loaded = gittide::ProjectStore::from_json(legacy);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].repos.size() == 1);
    REQUIRE(loaded->projects()[0].sources.empty());
    REQUIRE(loaded->loadedVersion() == gittide::ProjectStore::kVersion);
}

TEST_CASE("malformed source entries are skipped, not fatal", "[store][sources]")
{
    const std::string doc = R"({
      "version": 1,
      "projects": [ { "id": "uuid-1", "name": "Work", "sources": [ 42, { "path": "/home/u/projects" } ] } ]
    })";

    auto loaded = gittide::ProjectStore::from_json(doc);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects()[0].sources.size() == 1);
    REQUIRE(loaded->projects()[0].sources[0].path == "/home/u/projects");
    REQUIRE(loaded->projects()[0].sources[0].maxDepth == 2); // default
}

TEST_CASE("addSource appends a source to the project", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");

    auto result = store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects", .maxDepth = 3});

    REQUIRE(result.has_value());
    REQUIRE(store.projects()[0].sources.size() == 1);
    REQUIRE(store.projects()[0].sources[0].maxDepth == 3);
}

TEST_CASE("addSource rejects a duplicate source path", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());

    auto again = store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"});

    REQUIRE_FALSE(again.has_value());
    REQUIRE(store.projects()[0].sources.size() == 1);
}

TEST_CASE("addSource returns an error for an unknown project id", "[store][sources]")
{
    gittide::ProjectStore store;
    REQUIRE_FALSE(store.addSource("nope", gittide::RepoSource{.path = "/home/u/projects"}).has_value());
}

TEST_CASE("removeSource drops the source but keeps the repos it added", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());
    REQUIRE(store.addRepo(p.id, gittide::RepoRef{.path = "/home/u/projects/api"}).has_value());

    auto result = store.removeSource(p.id, "/home/u/projects");

    REQUIRE(result.has_value());
    REQUIRE(store.projects()[0].sources.empty());
    REQUIRE(store.projects()[0].repos.size() == 1);
}

TEST_CASE("removeSource errors on an unknown source path", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE_FALSE(store.removeSource(p.id, "/home/u/projects").has_value());
}

TEST_CASE("ignoreInSources records the repo under the containing source", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/work"}).has_value());

    store.ignoreInSources(p.id, "/home/u/projects/api");

    REQUIRE(store.projects()[0].sources[0].ignored == std::vector<std::string>{"/home/u/projects/api"});
    REQUIRE(store.projects()[0].sources[1].ignored.empty());
}

TEST_CASE("ignoreInSources matches on directory boundaries only", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/proj"}).has_value());

    store.ignoreInSources(p.id, "/home/u/projects/api"); // "/home/u/proj" is NOT a parent

    REQUIRE(store.projects()[0].sources[0].ignored.empty());
}

TEST_CASE("ignoreInSources records a source's own path when it is itself the repository",
          "[store][sources]")
{
    // scanForRepos(root) returns root itself as the sole result when root is a
    // repository, so a source can be registered with path == the repo's own
    // path. isUnder() alone never matches this (child.size() <= parent.size()
    // is false by construction) — without this case, removing that repo could
    // never be recorded as ignored, and the next rescan would re-add it forever.
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects/api"}).has_value());

    store.ignoreInSources(p.id, "/home/u/projects/api");

    REQUIRE(store.projects()[0].sources[0].ignored == std::vector<std::string>{"/home/u/projects/api"});
}

TEST_CASE("ignoreInSources never records the same path twice", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());

    store.ignoreInSources(p.id, "/home/u/projects/api");
    store.ignoreInSources(p.id, "/home/u/projects/api");

    REQUIRE(store.projects()[0].sources[0].ignored.size() == 1);
}

TEST_CASE("ignoreInSources on an unknown project is a no-op", "[store][sources]")
{
    gittide::ProjectStore store;
    store.ignoreInSources("nope", "/home/u/projects/api"); // must not throw
    REQUIRE(store.projects().empty());
}

TEST_CASE("clearIgnored empties one source's ignore list", "[store][sources]")
{
    gittide::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(
        store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects", .maxDepth = 2, .ignored = {"/home/u/projects/api"}})
            .has_value());

    REQUIRE(store.clearIgnored(p.id, "/home/u/projects").has_value());

    REQUIRE(store.projects()[0].sources[0].ignored.empty());
    REQUIRE_FALSE(store.clearIgnored(p.id, "/home/u/nowhere").has_value());
}
