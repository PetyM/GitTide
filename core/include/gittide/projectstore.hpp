#pragma once
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "gittide/giterror.hpp"

namespace gittide {

struct RepoRef
{
    std::string path; // absolute, stored as UTF-8 generic path
    std::string alias;
};

/// A folder that is rescanned for repositories to add to a project.
struct RepoSource
{
    std::string path; ///< absolute, stored as UTF-8 generic path
    int maxDepth = 2; ///< see ScanOptions::maxDepth
    /// Repo paths this source must never add again — seeded from the repos the
    /// user left unchecked when registering, grown by removals from the project.
    std::vector<std::string> ignored;
};

struct Project
{
    std::string id;
    std::string name;
    std::vector<RepoRef> repos;
    /// Folders rescanned on project activation; see RepoSource.
    std::vector<RepoSource> sources;
    std::string lastActiveRepo;
};

// In-memory model of projects.json. Persistence (load/save to disk) in Task 9.
// Note: `activeProject` is the "last-focused" project hint (under the planned
// multi-window UI there can be several open at once); it is not an exclusive lock.
class ProjectStore
{
public:
    static constexpr int kVersion = 1;

    std::deque<Project>& projects()
    {
        return m_projects;
    }
    const std::deque<Project>& projects() const
    {
        return m_projects;
    }

    const std::string& activeProject() const
    {
        return m_activeProject;
    }
    void setActiveProject(std::string id)
    {
        m_activeProject = std::move(id);
    }

    // Schema version read from the parsed document (kVersion for an in-memory
    // store or a document with no "version" key). Lets a future migration step
    // detect older on-disk schemas.
    int loadedVersion() const
    {
        return m_loadedVersion;
    }

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

    // Register a folder as a repository source of the named project. Returns an
    // error if projectId is not found, or if a source with the same path already
    // exists in that project. Call save() after mutating to persist the change.
    Expected<void> addSource(const std::string& projectId, RepoSource src);

    // Unregister a source by path. The repositories it already added stay in the
    // project. Returns an error if the project or the source is not found.
    Expected<void> removeSource(const std::string& projectId, const std::string& path);

    // Record repoPath as ignored in every source that contains it, so a rescan
    // never re-adds a repository the user removed. A source's own folder
    // counts as containing repoPath when the folder is itself the repository
    // (equal paths); otherwise matching is on directory boundaries. A path
    // already recorded is not duplicated. Unknown project, or a path under no
    // source: no-op.
    void ignoreInSources(const std::string& projectId, const std::string& repoPath);

    // Empty one source's ignore list, so its next scan offers everything again.
    // Returns an error if the project or the source is not found.
    Expected<void> clearIgnored(const std::string& projectId, const std::string& sourcePath);

    // Remove a project by id. If it was the active project, activeProject is cleared.
    void removeProject(const std::string& id);

private:
    std::deque<Project> m_projects;
    std::string m_activeProject;
    int m_loadedVersion = kVersion;
};

} // namespace gittide
