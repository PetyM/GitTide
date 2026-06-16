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
