#pragma once
#include <filesystem>
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

    // Schema version read from the parsed document (kVersion for an in-memory
    // store or a document with no "version" key). Lets a future migration step
    // detect older on-disk schemas.
    int loadedVersion() const { return loadedVersion_; }

    std::string to_json() const;
    static Expected<ProjectStore> from_json(const std::string& json);

    // Save atomically (temp file + rename). Returns error on I/O failure.
    Expected<void> save(const std::filesystem::path& file) const;

    // Load from disk. Missing file -> empty store. Corrupt file -> back it up
    // to "<file>.corrupt" and return an empty store (never fails on bad data).
    static Expected<ProjectStore> load(const std::filesystem::path& file);

    // Append a new Project with a random unique id and the given name.
    // Returns a reference to the newly created project.
    // Call save() after mutating to persist the change.
    Project& createProject(const std::string& name);

private:
    std::vector<Project> projects_;
    std::string activeProject_;
    int loadedVersion_ = kVersion;
};

}  // namespace gitgui
