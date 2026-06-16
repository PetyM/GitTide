#include <catch2/catch_test_macros.hpp>
#include "gitgui/ProjectStore.hpp"

TEST_CASE("ProjectStore serializes and deserializes round-trip", "[store]") {
    gitgui::ProjectStore store;
    gitgui::Project p;
    p.id = "uuid-1";
    p.name = "Work";
    p.repos.push_back(gitgui::RepoRef{"/home/u/api", "api"});
    p.lastActiveRepo = "/home/u/api";
    store.projects().push_back(p);
    store.setActiveProject("uuid-1");

    std::string json = store.to_json();
    auto loaded = gitgui::ProjectStore::from_json(json);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->activeProject() == "uuid-1");
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "Work");
    REQUIRE(loaded->projects()[0].repos.size() == 1);
    REQUIRE(loaded->projects()[0].repos[0].alias == "api");
}

TEST_CASE("empty store round-trips to an empty store", "[store]") {
    gitgui::ProjectStore store;
    auto loaded = gitgui::ProjectStore::from_json(store.to_json());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
    REQUIRE(loaded->activeProject().empty());
    REQUIRE(loaded->loadedVersion() == gitgui::ProjectStore::kVersion);
}

TEST_CASE("malformed JSON returns an error, never throws", "[store]") {
    auto loaded = gitgui::ProjectStore::from_json("{ this is not json");
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(loaded.error().code != 0);
}

TEST_CASE("wrong-typed \"projects\" key degrades to an error, never throws", "[store]") {
    // "projects" present but not an array — must not throw past Expected.
    auto a = gitgui::ProjectStore::from_json(R"({"projects": null})");
    REQUIRE_FALSE(a.has_value());

    auto b = gitgui::ProjectStore::from_json(R"({"projects": "oops"})");
    REQUIRE_FALSE(b.has_value());
}

TEST_CASE("missing optional keys degrade to defaults", "[store]") {
    // A project object missing every field is tolerated with empty defaults.
    auto loaded = gitgui::ProjectStore::from_json(R"({"projects": [ {} ]})");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].id.empty());
    REQUIRE(loaded->projects()[0].repos.empty());
}

TEST_CASE("non-object root is rejected", "[store]") {
    auto loaded = gitgui::ProjectStore::from_json("[1, 2, 3]");
    REQUIRE_FALSE(loaded.has_value());
}

#include <filesystem>
#include <fstream>
#include <random>

namespace {
std::filesystem::path temp_json_path() {
    std::random_device rd;
    return std::filesystem::temp_directory_path() /
           ("gitgui_store_" + std::to_string(rd()) + ".json");
}
}

TEST_CASE("save then load round-trips through disk", "[store]") {
    auto path = temp_json_path();
    gitgui::ProjectStore store;
    gitgui::Project p; p.id = "x"; p.name = "Proj";
    store.projects().push_back(p);

    auto saved = store.save(path);
    REQUIRE(saved.has_value());

    auto loaded = gitgui::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "Proj");

    std::filesystem::remove(path);
}

TEST_CASE("load of a missing file returns an empty store", "[store]") {
    auto loaded = gitgui::ProjectStore::load(temp_json_path());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
}

TEST_CASE("load of corrupt JSON backs up the file and returns empty store", "[store]") {
    auto path = temp_json_path();
    { std::ofstream(path) << "{ this is not json"; }

    auto loaded = gitgui::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
    REQUIRE(std::filesystem::exists(path.string() + ".corrupt"));
    REQUIRE_FALSE(std::filesystem::exists(path));  // original was renamed away

    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".corrupt");
}
