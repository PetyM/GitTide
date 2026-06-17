#pragma once
#include <deque>
#include <filesystem>
#include <string>
#include <vector>
#include "gittide/GitError.hpp"

namespace gittide {

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

    std::deque<Project>& projects() { return projects_; }
    const std::deque<Project>& projects() const { return projects_; }

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

    // Add repo to the named project. Returns error if projectId is not found,
    // or if a repo with the same path already exists in that project.
    // Call save() after mutating to persist the change.
    Expected<void> addRepo(const std::string& projectId, RepoRef repo);

    // Remove a repo by path from the named project. Returns error if not found.
    Expected<void> removeRepo(const std::string& projectId, const std::string& path);

    // Remove a project by id. If it was the active project, activeProject is cleared.
    void removeProject(const std::string& id);

private:
    std::deque<Project> projects_;
    std::string activeProject_;
    int loadedVersion_ = kVersion;
};

}  // namespace gittide
