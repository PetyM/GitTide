#include "gittide/credentialsstore.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <system_error>

using json = nlohmann::json;

namespace gittide {

namespace {

// 32-hex-char random id, identical scheme to ProjectStore::createProject.
std::string makeId()
{
    static std::mt19937_64 gen{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << dist(gen) << std::setw(16) << dist(gen);
    return oss.str();
}

json toJson(const std::map<std::string, std::string>& m)
{
    json obj = json::object();
    for (const auto& [k, v] : m)
        obj[k] = v;
    return obj;
}

void fromJson(std::map<std::string, std::string>& out, const json& obj)
{
    if (!obj.is_object())
        return;
    for (const auto& [k, v] : obj.items())
    {
        if (v.is_string())
            out[k] = v.get<std::string>();
    }
}

} // namespace

std::string CredentialsStore::projectDefault(const std::string& projectId) const
{
    auto it = m_projectDefaults.find(projectId);
    return it == m_projectDefaults.end() ? std::string{} : it->second;
}

void CredentialsStore::setProjectDefault(const std::string& projectId, std::string identityId)
{
    if (identityId.empty())
        m_projectDefaults.erase(projectId);
    else
        m_projectDefaults[projectId] = std::move(identityId);
}

void CredentialsStore::clearProjectDefault(const std::string& projectId)
{
    m_projectDefaults.erase(projectId);
}

std::string CredentialsStore::repoOverride(const std::string& repoPath) const
{
    auto it = m_repoOverrides.find(repoPath);
    return it == m_repoOverrides.end() ? std::string{} : it->second;
}

void CredentialsStore::setRepoOverride(const std::string& repoPath, std::string identityId)
{
    if (identityId.empty())
        m_repoOverrides.erase(repoPath);
    else
        m_repoOverrides[repoPath] = std::move(identityId);
}

void CredentialsStore::clearRepoOverride(const std::string& repoPath)
{
    m_repoOverrides.erase(repoPath);
}

GitIdentity& CredentialsStore::addIdentity(const std::string& name, const std::string& email)
{
    m_identities.push_back(GitIdentity{makeId(), name, email});
    return m_identities.back();
}

void CredentialsStore::updateIdentity(const std::string& id, const std::string& name, const std::string& email)
{
    for (auto& i : m_identities)
    {
        if (i.id == id)
        {
            i.name  = name;
            i.email = email;
            return;
        }
    }
}

void CredentialsStore::removeIdentity(const std::string& id)
{
    std::erase_if(m_identities, [&](const GitIdentity& i) { return i.id == id; });

    if (m_globalIdentity == id)
        m_globalIdentity.clear();
    std::erase_if(m_projectDefaults, [&](const auto& kv) { return kv.second == id; });
    std::erase_if(m_repoOverrides, [&](const auto& kv) { return kv.second == id; });
    for (auto& h : m_hosts)
    {
        if (h.identityId == id)
            h.identityId.clear();
    }
}

SshKeyRef& CredentialsStore::addSshKey(const std::string& label, const std::string& publicKeyPath,
                                       const std::string& privateKeyPath, bool hasPassphrase)
{
    m_sshKeys.push_back(SshKeyRef{makeId(), label, publicKeyPath, privateKeyPath, hasPassphrase});
    return m_sshKeys.back();
}

void CredentialsStore::removeSshKey(const std::string& id)
{
    std::erase_if(m_sshKeys, [&](const SshKeyRef& k) { return k.id == id; });
}

HostAccount& CredentialsStore::addHost(const std::string& host, const std::string& kind, const std::string& apiBase,
                                       const std::string& username, const std::string& authType,
                                       const std::string& identityId)
{
    m_hosts.push_back(HostAccount{makeId(), host, kind, apiBase, username, authType, identityId});
    return m_hosts.back();
}

void CredentialsStore::removeHost(const std::string& id)
{
    std::erase_if(m_hosts, [&](const HostAccount& h) { return h.id == id; });
}

const GitIdentity* CredentialsStore::findIdentity(const std::string& id) const
{
    for (const auto& i : m_identities)
    {
        if (i.id == id)
            return &i;
    }
    return nullptr;
}

std::optional<GitIdentity> CredentialsStore::resolveLocalIdentity(
    std::string_view repoPath, std::span<const std::string> candidateProjectIdsInPriorityOrder) const
{
    // 1. Per-repo override — the unambiguous escape hatch.
    if (auto it = m_repoOverrides.find(std::string(repoPath)); it != m_repoOverrides.end())
    {
        if (const GitIdentity* id = findIdentity(it->second))
            return *id;
    }
    // 2. Project default — first candidate (active project first) that defines one.
    for (const std::string& projectId : candidateProjectIdsInPriorityOrder)
    {
        if (auto it = m_projectDefaults.find(projectId); it != m_projectDefaults.end())
        {
            if (const GitIdentity* id = findIdentity(it->second))
                return *id;
        }
    }
    return std::nullopt;
}

std::optional<GitIdentity> CredentialsStore::resolveIdentity(
    std::string_view repoPath, std::span<const std::string> candidateProjectIdsInPriorityOrder) const
{
    if (auto local = resolveLocalIdentity(repoPath, candidateProjectIdsInPriorityOrder))
        return local;
    // Fall back to the global default (used for display, not for local materialization).
    if (const GitIdentity* id = findIdentity(m_globalIdentity))
        return *id;
    return std::nullopt;
}

std::string CredentialsStore::to_json() const
{
    json root;
    root["version"]        = kVersion;
    root["globalIdentity"] = m_globalIdentity;

    json ids = json::array();
    for (const auto& i : m_identities)
        ids.push_back({{"id", i.id}, {"name", i.name}, {"email", i.email}});
    root["identities"] = std::move(ids);

    json keys = json::array();
    for (const auto& k : m_sshKeys)
        keys.push_back({{"id", k.id},
                        {"label", k.label},
                        {"publicKeyPath", k.publicKeyPath},
                        {"privateKeyPath", k.privateKeyPath},
                        {"hasPassphrase", k.hasPassphrase}});
    root["sshKeys"] = std::move(keys);

    json hosts = json::array();
    for (const auto& h : m_hosts)
        hosts.push_back({{"id", h.id},
                         {"host", h.host},
                         {"kind", h.kind},
                         {"apiBase", h.apiBase},
                         {"username", h.username},
                         {"authType", h.authType},
                         {"identityId", h.identityId}});
    root["hosts"] = std::move(hosts);

    json assignments;
    assignments["projectDefaults"] = toJson(m_projectDefaults);
    assignments["repoOverrides"]   = toJson(m_repoOverrides);
    root["assignments"]            = std::move(assignments);

    return root.dump(2);
}

Expected<CredentialsStore> CredentialsStore::from_json(const std::string& text)
{
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded())
        return std::unexpected(GitError{-1, "invalid JSON in credentials store"});
    if (!root.is_object())
        return std::unexpected(GitError{-1, "credentials store root is not a JSON object"});

    // A hand-edited or externally produced file may have keys of the wrong type;
    // catch json::type_error so a malformed document degrades to an error, never a crash.
    try
    {
        CredentialsStore store;
        store.m_loadedVersion  = root.value("version", kVersion);
        store.m_globalIdentity = root.value("globalIdentity", std::string{});

        if (root.contains("identities"))
        {
            const json& arr = root.at("identities");
            if (!arr.is_array())
                return std::unexpected(GitError{-1, "\"identities\" is not an array"});
            for (const auto& j : arr)
            {
                if (!j.is_object())
                    continue;
                store.m_identities.push_back(GitIdentity{
                    j.value("id", std::string{}), j.value("name", std::string{}), j.value("email", std::string{})});
            }
        }

        if (root.contains("sshKeys"))
        {
            const json& arr = root.at("sshKeys");
            if (!arr.is_array())
                return std::unexpected(GitError{-1, "\"sshKeys\" is not an array"});
            for (const auto& j : arr)
            {
                if (!j.is_object())
                    continue;
                store.m_sshKeys.push_back(SshKeyRef{j.value("id", std::string{}),
                                                    j.value("label", std::string{}),
                                                    j.value("publicKeyPath", std::string{}),
                                                    j.value("privateKeyPath", std::string{}),
                                                    j.value("hasPassphrase", false)});
            }
        }

        if (root.contains("hosts"))
        {
            const json& arr = root.at("hosts");
            if (!arr.is_array())
                return std::unexpected(GitError{-1, "\"hosts\" is not an array"});
            for (const auto& j : arr)
            {
                if (!j.is_object())
                    continue;
                store.m_hosts.push_back(HostAccount{j.value("id", std::string{}),
                                                    j.value("host", std::string{}),
                                                    j.value("kind", std::string{}),
                                                    j.value("apiBase", std::string{}),
                                                    j.value("username", std::string{}),
                                                    j.value("authType", std::string{}),
                                                    j.value("identityId", std::string{})});
            }
        }

        if (root.contains("assignments") && root.at("assignments").is_object())
        {
            const json& a = root.at("assignments");
            if (a.contains("projectDefaults"))
                fromJson(store.m_projectDefaults, a.at("projectDefaults"));
            if (a.contains("repoOverrides"))
                fromJson(store.m_repoOverrides, a.at("repoOverrides"));
        }

        return store;
    }
    catch (const json::exception& e)
    {
        return std::unexpected(GitError{-1, std::string("malformed credentials store: ") + e.what()});
    }
}

Expected<void> CredentialsStore::save(const std::filesystem::path& file) const
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
    // ec intentionally ignored: an already-existing dir succeeds silently; a real
    // failure surfaces as a rename error below.

    std::filesystem::rename(tmp, file, ec);
    if (ec)
    {
        std::filesystem::remove(tmp); // best-effort cleanup of stale .tmp
        return std::unexpected(GitError{-1, "atomic rename failed: " + ec.message()});
    }
    return {};
}

Expected<CredentialsStore> CredentialsStore::load(const std::filesystem::path& file)
{
    std::error_code ec;
    bool present = std::filesystem::exists(file, ec);
    if (ec)
        return std::unexpected(GitError{-1, "cannot stat credentials store: " + ec.message()});
    if (!present)
        return CredentialsStore{}; // missing file -> empty store

    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return std::unexpected(GitError{-1, "cannot open credentials store for read"});
        text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    } // close the handle before any rename (Windows refuses to rename an open file).

    auto parsed = from_json(text);
    if (!parsed.has_value())
    {
        // Corrupt data: back the file up and return an empty store — bad data must
        // never prevent the app from starting.
        std::filesystem::path backup = file;
        backup += ".corrupt";
        std::filesystem::rename(file, backup, ec); // best-effort; ignore ec
        return CredentialsStore{};
    }
    return parsed;
}

} // namespace gittide
