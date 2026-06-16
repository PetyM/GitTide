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
    if (!root.is_object())
        return std::unexpected(GitError{-1, "project store root is not a JSON object"});

    // A hand-edited or externally produced file may have keys of the wrong type
    // (e.g. "projects": null). value()/at() throw json::type_error on a mismatch;
    // catch it so a malformed document degrades to an error rather than crashing.
    try {
        ProjectStore store;
        store.loadedVersion_ = root.value("version", kVersion);
        store.activeProject_ = root.value("activeProject", std::string{});

        if (root.contains("projects")) {
            const json& projects = root.at("projects");
            if (!projects.is_array())
                return std::unexpected(GitError{-1, "\"projects\" is not an array"});
            for (const auto& jp : projects) {
                if (!jp.is_object()) continue;  // skip malformed project entries
                Project p;
                p.id = jp.value("id", std::string{});
                p.name = jp.value("name", std::string{});
                p.lastActiveRepo = jp.value("lastActiveRepo", std::string{});
                if (jp.contains("repos") && jp.at("repos").is_array()) {
                    for (const auto& jr : jp.at("repos")) {
                        if (!jr.is_object()) continue;
                        p.repos.push_back(RepoRef{jr.value("path", std::string{}),
                                                  jr.value("alias", std::string{})});
                    }
                }
                store.projects_.push_back(std::move(p));
            }
        }
        return store;
    } catch (const json::exception& e) {
        return std::unexpected(GitError{-1, std::string("malformed project store: ") + e.what()});
    }
}

}  // namespace gitgui
