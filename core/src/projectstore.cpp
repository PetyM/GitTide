#include "gittide/projectstore.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <system_error>

using json = nlohmann::json;

namespace {

// True when `child` lies inside directory `parent` — a plain prefix test is
// wrong ("/home/u/proj" would swallow "/home/u/projects/api"), so the prefix
// must end on a separator. Paths here are always generic (forward-slash) form.
bool isUnder(const std::string& parent, const std::string& child)
{
    if (parent.empty() || child.size() <= parent.size())
        return false;
    if (child.compare(0, parent.size(), parent) != 0)
        return false;
    const bool parentEndsWithSlash = parent.back() == '/';
    return parentEndsWithSlash || child[parent.size()] == '/';
}

} // namespace

namespace gittide {

std::string ProjectStore::to_json() const
{
    json root;
    root["version"]       = kVersion;
    root["activeProject"] = m_activeProject;
    json arr              = json::array();
    for (const auto& p : m_projects)
    {
        json jp;
        jp["id"]             = p.id;
        jp["name"]           = p.name;
        jp["lastActiveRepo"] = p.lastActiveRepo;
        json repos           = json::array();
        for (const auto& r : p.repos)
        {
            repos.push_back({{"path", r.path}, {"alias", r.alias}});
        }
        jp["repos"] = std::move(repos);

        json sources = json::array();
        for (const auto& s : p.sources)
        {
            sources.push_back({{"path", s.path}, {"maxDepth", s.maxDepth}, {"ignored", s.ignored}});
        }
        jp["sources"] = std::move(sources);

        arr.push_back(std::move(jp));
    }
    root["projects"] = std::move(arr);
    return root.dump(2);
}

Expected<ProjectStore> ProjectStore::from_json(const std::string& text)
{
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded())
        return std::unexpected(GitError{-1, "invalid JSON in project store"});
    if (!root.is_object())
        return std::unexpected(GitError{-1, "project store root is not a JSON object"});

    // A hand-edited or externally produced file may have keys of the wrong type
    // (e.g. "projects": null). value()/at() throw json::type_error on a mismatch;
    // catch it so a malformed document degrades to an error rather than crashing.
    try
    {
        ProjectStore store;
        store.m_loadedVersion = root.value("version", kVersion);
        store.m_activeProject = root.value("activeProject", std::string{});

        if (root.contains("projects"))
        {
            const json& projects = root.at("projects");
            if (!projects.is_array())
                return std::unexpected(GitError{-1, "\"projects\" is not an array"});
            for (const auto& jp : projects)
            {
                if (!jp.is_object())
                    continue; // skip malformed project entries
                Project p;
                p.id             = jp.value("id", std::string{});
                p.name           = jp.value("name", std::string{});
                p.lastActiveRepo = jp.value("lastActiveRepo", std::string{});
                if (jp.contains("repos") && jp.at("repos").is_array())
                {
                    for (const auto& jr : jp.at("repos"))
                    {
                        if (!jr.is_object())
                            continue;
                        p.repos.push_back(RepoRef{jr.value("path", std::string{}), jr.value("alias", std::string{})});
                    }
                }
                // "sources" is additive to the v1 schema: a document written
                // before sources existed simply has none.
                if (jp.contains("sources") && jp.at("sources").is_array())
                {
                    for (const auto& js : jp.at("sources"))
                    {
                        if (!js.is_object())
                            continue; // skip malformed source entries
                        RepoSource s;
                        s.path     = js.value("path", std::string{});
                        s.maxDepth = js.value("maxDepth", 2);
                        if (js.contains("ignored") && js.at("ignored").is_array())
                        {
                            for (const auto& ji : js.at("ignored"))
                            {
                                if (ji.is_string())
                                    s.ignored.push_back(ji.get<std::string>());
                            }
                        }
                        if (!s.path.empty())
                            p.sources.push_back(std::move(s));
                    }
                }
                store.m_projects.push_back(std::move(p));
            }
        }
        return store;
    }
    catch (const json::exception& e)
    {
        return std::unexpected(GitError{-1, std::string("malformed project store: ") + e.what()});
    }
}

Expected<void> ProjectStore::save(const std::filesystem::path& file) const
{
    // Write to a temp file in the same directory so rename is atomic (same fs).
    std::filesystem::path tmp = file;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return std::unexpected(GitError{-1, "cannot open temp file for write"});
        out << to_json();
        if (!out)
            return std::unexpected(GitError{-1, "write to temp file failed"});
    } // flush + close before rename

    std::error_code ec;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    // ec from create_directories is intentionally ignored: if the dir already
    // exists create_directories succeeds silently; any real failure will surface
    // as a rename error below.

    // rename-over-existing is atomic on POSIX and on modern Windows (where
    // std::filesystem::rename performs a replace). We deliberately do not fsync
    // the temp file before renaming: for a small project-registry file the
    // durability gap on a hard crash is acceptable (worst case is losing the most
    // recent save, never corrupting the prior on-disk copy).
    std::filesystem::rename(tmp, file, ec);
    if (ec)
    {
        std::filesystem::remove(tmp); // best-effort cleanup of stale .tmp
        return std::unexpected(GitError{-1, "atomic rename failed: " + ec.message()});
    }
    return {};
}

Expected<ProjectStore> ProjectStore::load(const std::filesystem::path& file)
{
    std::error_code ec;
    bool present = std::filesystem::exists(file, ec);
    // A stat failure (e.g. permission denied on the path) is hard I/O, not
    // "missing" — surface it rather than masking it as an empty store.
    if (ec)
        return std::unexpected(GitError{-1, "cannot stat project store: " + ec.message()});
    if (!present)
        return ProjectStore{}; // missing file -> empty store

    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return std::unexpected(GitError{-1, "cannot open project store for read"});
        text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    } // close the handle before any rename: Windows refuses to rename an open file.

    auto parsed = from_json(text);
    if (!parsed.has_value())
    {
        // Corrupt data: back the file up, return an empty store — never propagate
        // a parse error to callers (bad data must not prevent the app from starting).
        // An existing "<file>.corrupt" is intentionally overwritten (POSIX rename
        // replaces the destination): we keep only the most recent bad copy.
        std::filesystem::path backup = file;
        backup += ".corrupt";
        std::filesystem::rename(file, backup, ec); // best-effort; ignore ec
        return ProjectStore{};
    }
    return parsed;
}

Project& ProjectStore::createProject(const std::string& name)
{
    static std::mt19937_64 gen{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << dist(gen) << std::setw(16) << dist(gen);
    Project p;
    p.id   = oss.str();
    p.name = name;
    m_projects.push_back(std::move(p));
    return m_projects.back();
}

Expected<void> ProjectStore::addRepo(const std::string& projectId, RepoRef repo)
{
    auto it = std::find_if(m_projects.begin(),
                           m_projects.end(),
                           [&](const Project& p)
                           {
                               return p.id == projectId;
                           });
    if (it == m_projects.end())
        return std::unexpected(GitError{-1, "project not found: " + projectId});

    for (const auto& existing : it->repos)
    {
        if (existing.path == repo.path)
            return std::unexpected(GitError{-1, "repository already in project: " + repo.path});
    }
    it->repos.push_back(std::move(repo));
    return {};
}

Expected<void> ProjectStore::removeRepo(const std::string& projectId, const std::string& path)
{
    auto it = std::find_if(m_projects.begin(),
                           m_projects.end(),
                           [&](const Project& p)
                           {
                               return p.id == projectId;
                           });
    if (it == m_projects.end())
        return std::unexpected(GitError{-1, "project not found: " + projectId});
    auto& repos = it->repos;
    auto r      = std::find_if(repos.begin(),
                          repos.end(),
                          [&](const RepoRef& ref)
                          {
                              return ref.path == path;
                          });
    if (r == repos.end())
        return std::unexpected(GitError{-1, "repo not found: " + path});
    repos.erase(r);
    return {};
}

Expected<void> ProjectStore::addSource(const std::string& projectId, RepoSource src)
{
    auto it = std::find_if(m_projects.begin(),
                           m_projects.end(),
                           [&](const Project& p)
                           {
                               return p.id == projectId;
                           });
    if (it == m_projects.end())
        return std::unexpected(GitError{-1, "project not found: " + projectId});

    for (const auto& existing : it->sources)
    {
        if (existing.path == src.path)
            return std::unexpected(GitError{-1, "source already registered: " + src.path});
    }
    it->sources.push_back(std::move(src));
    return {};
}

Expected<void> ProjectStore::removeSource(const std::string& projectId, const std::string& path)
{
    auto it = std::find_if(m_projects.begin(),
                           m_projects.end(),
                           [&](const Project& p)
                           {
                               return p.id == projectId;
                           });
    if (it == m_projects.end())
        return std::unexpected(GitError{-1, "project not found: " + projectId});

    auto& sources = it->sources;
    auto s        = std::find_if(sources.begin(),
                          sources.end(),
                          [&](const RepoSource& src)
                          {
                              return src.path == path;
                          });
    if (s == sources.end())
        return std::unexpected(GitError{-1, "source not found: " + path});

    // Deliberately leaves it->repos alone: unregistering a source must not
    // silently drop repositories the user is working in.
    sources.erase(s);
    return {};
}

void ProjectStore::ignoreInSources(const std::string& projectId, const std::string& repoPath)
{
    auto it = std::find_if(m_projects.begin(),
                           m_projects.end(),
                           [&](const Project& p)
                           {
                               return p.id == projectId;
                           });
    if (it == m_projects.end())
        return;

    for (auto& s : it->sources)
    {
        // A source's own folder counts as "containing" the repo when the
        // folder is itself the repository (scanForRepos returns the root as
        // the sole candidate in that case, so addRepos registers a source
        // whose path equals the repo's path) — isUnder() alone never matches
        // that (child.size() <= parent.size() is false by construction).
        if (s.path != repoPath && !isUnder(s.path, repoPath))
            continue;
        if (std::find(s.ignored.begin(), s.ignored.end(), repoPath) == s.ignored.end())
            s.ignored.push_back(repoPath);
    }
}

Expected<void> ProjectStore::clearIgnored(const std::string& projectId, const std::string& sourcePath)
{
    auto it = std::find_if(m_projects.begin(),
                           m_projects.end(),
                           [&](const Project& p)
                           {
                               return p.id == projectId;
                           });
    if (it == m_projects.end())
        return std::unexpected(GitError{-1, "project not found: " + projectId});

    auto s = std::find_if(it->sources.begin(),
                          it->sources.end(),
                          [&](const RepoSource& src)
                          {
                              return src.path == sourcePath;
                          });
    if (s == it->sources.end())
        return std::unexpected(GitError{-1, "source not found: " + sourcePath});

    s->ignored.clear();
    return {};
}

void ProjectStore::removeProject(const std::string& id)
{
    m_projects.erase(std::remove_if(m_projects.begin(),
                                    m_projects.end(),
                                    [&](const Project& p)
                                    {
                                        return p.id == id;
                                    }),
                     m_projects.end());
    if (m_activeProject == id)
        m_activeProject.clear();
}

} // namespace gittide
