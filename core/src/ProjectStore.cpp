#include "gitgui/ProjectStore.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace gitgui {

std::string ProjectStore::to_json() const {
    json root;
    root["version"] = kVersion;
    root["activeProject"] = activeProject_;
    json arr = json::array();
    for (const auto& p : projects_) {
        json jp;
        jp["id"] = p.id;
        jp["name"] = p.name;
        jp["lastActiveRepo"] = p.lastActiveRepo;
        json repos = json::array();
        for (const auto& r : p.repos) {
            repos.push_back({{"path", r.path}, {"alias", r.alias}});
        }
        jp["repos"] = std::move(repos);
        arr.push_back(std::move(jp));
    }
    root["projects"] = std::move(arr);
    return root.dump(2);
}

Expected<ProjectStore> ProjectStore::from_json(const std::string& text) {
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded())
        return std::unexpected(GitError{-1, "invalid JSON in project store"});

    ProjectStore store;
    store.activeProject_ = root.value("activeProject", std::string{});
    for (const auto& jp : root.value("projects", json::array())) {
        Project p;
        p.id = jp.value("id", std::string{});
        p.name = jp.value("name", std::string{});
        p.lastActiveRepo = jp.value("lastActiveRepo", std::string{});
        for (const auto& jr : jp.value("repos", json::array())) {
            p.repos.push_back(RepoRef{jr.value("path", std::string{}),
                                      jr.value("alias", std::string{})});
        }
        store.projects_.push_back(std::move(p));
    }
    return store;
}

}  // namespace gitgui
