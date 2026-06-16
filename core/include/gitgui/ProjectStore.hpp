#pragma once
#include <string>
#include <vector>
#include "gitgui/GitError.hpp"

namespace gitgui {

struct RepoRef {
    std::string path;    // absolute, stored as UTF-8 generic path
    std::string alias;
};

struct Project {
    std::string id;
    std::string name;
    std::vector<RepoRef> repos;
    std::string lastActiveRepo;
};

// In-memory model of projects.json. Persistence (load/save to disk) in Task 9.
// Note: `activeProject` is the "last-focused" project hint (under the planned
// multi-window UI there can be several open at once); it is not an exclusive lock.
class ProjectStore {
public:
    static constexpr int kVersion = 1;

    std::vector<Project>& projects() { return projects_; }
    const std::vector<Project>& projects() const { return projects_; }

    const std::string& activeProject() const { return activeProject_; }
    void setActiveProject(std::string id) { activeProject_ = std::move(id); }

    std::string to_json() const;
    static Expected<ProjectStore> from_json(const std::string& json);

private:
    std::vector<Project> projects_;
    std::string activeProject_;
};

}  // namespace gitgui
