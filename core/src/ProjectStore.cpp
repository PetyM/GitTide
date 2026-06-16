#include "gitgui/ProjectStore.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
#include <system_error>

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

Expected<void> ProjectStore::save(const std::filesystem::path& file) const {
    // Write to a temp file in the same directory so rename is atomic (same fs).
    std::filesystem::path tmp = file;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(GitError{-1, "cannot open temp file for write"});
        out << to_json();
        if (!out) return std::unexpected(GitError{-1, "write to temp file failed"});
    }  // flush + close before rename

    std::error_code ec;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    // ec from create_directories is intentionally ignored: if the dir already
    // exists create_directories succeeds silently; any real failure will surface
    // as a rename error below.

    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp);  // best-effort cleanup of stale .tmp
        return std::unexpected(GitError{-1, "atomic rename failed: " + ec.message()});
    }
    return {};
}

Expected<ProjectStore> ProjectStore::load(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return ProjectStore{};  // missing file -> empty store

    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::unexpected(GitError{-1, "cannot open project store for read"});

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    auto parsed = from_json(text);
    if (!parsed.has_value()) {
        // Corrupt data: back the file up, return an empty store — never propagate
        // a parse error to callers (bad data must not prevent the app from starting).
        std::filesystem::path backup = file;
        backup += ".corrupt";
        std::filesystem::rename(file, backup, ec);  // best-effort; ignore ec
        return ProjectStore{};
    }
    return parsed;
}

}  // namespace gitgui
