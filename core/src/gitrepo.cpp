#include "gittide/gitrepo.hpp"

#include <git2.h>
#include <git2/annotated_commit.h>
#include <git2/branch.h>
#include <git2/checkout.h>
#include <git2/commit.h>
#include <git2/config.h>
#include <git2/credential.h>
#include <git2/graph.h>
#include <git2/merge.h>
#include <git2/rebase.h>
#include <git2/remote.h>
#include <git2/reset.h>
#include <git2/revwalk.h>
#include <git2/stash.h>
#include <git2/worktree.h>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gittide/diffengine.hpp"
#include "gittide/log.hpp"
#include "gittide/pathutil.hpp"
#include "gittide/sync.hpp"

namespace {
// True when the selection targets the whole file (no hunk chosen).
bool isWholeFile(const gittide::StageSelection& sel)
{
    return !sel.hunkIndex.has_value();
}

gittide::StatusFlag mapStatus(unsigned int s)
{
    using gittide::StatusFlag;
    StatusFlag f = StatusFlag::None;
    if (s & GIT_STATUS_INDEX_NEW)
        f |= StatusFlag::IndexNew;
    if (s & GIT_STATUS_INDEX_MODIFIED)
        f |= StatusFlag::IndexModified;
    if (s & GIT_STATUS_INDEX_DELETED)
        f |= StatusFlag::IndexDeleted;
    if (s & GIT_STATUS_WT_NEW)
        f |= StatusFlag::WtNew;
    if (s & GIT_STATUS_WT_MODIFIED)
        f |= StatusFlag::WtModified;
    if (s & GIT_STATUS_WT_DELETED)
        f |= StatusFlag::WtDeleted;
    return f;
}

// Maps each local branch checked out in a *linked* worktree to that worktree's
// path. Branches not held by any linked worktree are absent from the map.
std::map<std::string, std::string> worktreeBranchPaths(git_repository* repo)
{
    std::map<std::string, std::string> out;
    git_strarray names = {};
    if (git_worktree_list(&names, repo) != 0)
        return out;
    for (std::size_t i = 0; i < names.count; ++i)
    {
        git_worktree* wt = nullptr;
        if (git_worktree_lookup(&wt, repo, names.strings[i]) != 0)
            continue;
        std::unique_ptr<git_worktree, decltype(&git_worktree_free)> wt_guard(wt, git_worktree_free);
        git_repository* wt_repo = nullptr;
        if (git_repository_open_from_worktree(&wt_repo, wt) != 0)
            continue;
        std::unique_ptr<git_repository, decltype(&git_repository_free)> repo_guard(wt_repo, git_repository_free);
        git_reference* head = nullptr;
        if (git_repository_head(&head, wt_repo) == 0 && head)
        {
            std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);
            if (git_reference_is_branch(head) == 1)
            {
                const char* sh = git_reference_shorthand(head);
                const char* p  = git_worktree_path(wt);
                if (sh && p)
                    out[sh] = p;
            }
        }
    }
    git_strarray_dispose(&names);
    return out;
}

// Shared payload passed to ALL libgit2 callbacks on a git_remote_callbacks.
// libgit2 routes the same void* to both transfer_progress and credentials.
struct CbPayload
{
    gittide::ProgressCallback* cb   = nullptr;
    gittide::Credentials*      cred = nullptr;
};

int transferProgressTrampoline(const git_indexer_progress* stats, void* payload)
{
    auto* pl = static_cast<CbPayload*>(payload);
    return (*pl->cb)(stats->received_objects, stats->total_objects) ? 0 : -1;
}

int credentialTrampoline(git_credential** out, const char* url, const char* username_from_url,
                         unsigned int allowed_types, void* payload)
{
    auto* pl = static_cast<CbPayload*>(payload);
    if (!pl->cred)
        return GIT_EAUTH;
    const auto& cred = *pl->cred;
    switch (gittide::chooseCredential(url ? url : "", allowed_types, cred))
    {
    case gittide::CredentialKind::SshAgent:
        return git_credential_ssh_key_from_agent(out, username_from_url ? username_from_url : "git");
    case gittide::CredentialKind::UserPass:
        return git_credential_userpass_plaintext_new(out, cred.username.c_str(), cred.password.c_str());
    case gittide::CredentialKind::None:
    default:
        return GIT_EAUTH;
    }
}
} // anonymous namespace

namespace gittide {

GitRepo::GitRepo(GitRepo&& o) noexcept
    : m_repo(std::exchange(o.m_repo, nullptr))
{
}

GitRepo& GitRepo::operator=(GitRepo&& o) noexcept
{
    if (this != &o)
    {
        if (m_repo)
            git_repository_free(m_repo);
        m_repo = std::exchange(o.m_repo, nullptr);
    }
    return *this;
}

GitRepo::~GitRepo()
{
    if (m_repo)
        git_repository_free(m_repo);
}

Expected<GitRepo> GitRepo::open(const std::filesystem::path& path)
{
    git_repository* repo = nullptr;
    int rc               = git_repository_open(&repo, toGitPath(path).c_str());
    if (rc < 0)
    {
        const GitError err = lastGitError(rc);
        logf(LogLevel::Warning, logcat::GIT, "open '{}' failed ({}): {}", toGitPath(path), err.code, err.message);
        return std::unexpected(err);
    }
    logf(LogLevel::Debug, logcat::GIT, "opened repository '{}'", toGitPath(path));
    return GitRepo(repo);
}

Expected<GitRepo> GitRepo::init(const std::filesystem::path& path)
{
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags                       = GIT_REPOSITORY_INIT_NO_REINIT | GIT_REPOSITORY_INIT_MKPATH;
    git_repository* repo             = nullptr;
    int rc                           = git_repository_init_ext(&repo, toGitPath(path).c_str(), &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return GitRepo(repo);
}

Expected<GitRepo> GitRepo::clone(const std::string& url, const std::filesystem::path& dest, ProgressCallback cb)
{
    CbPayload pl{&cb, nullptr};
    git_clone_options opts                      = GIT_CLONE_OPTIONS_INIT;
    opts.fetch_opts.callbacks.transfer_progress = transferProgressTrampoline;
    opts.fetch_opts.callbacks.payload           = &pl;

    git_repository* repo = nullptr;
    int rc               = git_clone(&repo, url.c_str(), toGitPath(dest).c_str(), &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return GitRepo(repo);
}

Expected<std::vector<FileStatus>> GitRepo::status() const
{
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show               = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags              = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list* raw_list = nullptr;
    int rc                    = git_status_list_new(&raw_list, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    // RAII so the list is freed even if a vector allocation below throws.
    std::unique_ptr<git_status_list, decltype(&git_status_list_free)> list(raw_list, git_status_list_free);

    std::vector<FileStatus> result;
    size_t n = git_status_list_entrycount(list.get());
    result.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        const git_status_entry* e = git_status_byindex(list.get(), i);
        const char* raw           = e->head_to_index      ? e->head_to_index->new_file.path
                                    : e->index_to_workdir ? e->index_to_workdir->new_file.path
                                                          : nullptr;
        if (!raw)
            continue;
        StatusFlag flags = mapStatus(e->status);
        // Skip statuses this milestone does not model (rename, typechange,
        // conflict, ignored) — emitting them with flags==None would mislead
        // callers. Add the corresponding StatusFlag values to represent them.
        if (flags == StatusFlag::None)
            continue;
        result.push_back(FileStatus{fromGitPath(raw), flags});
    }
    return result;
}

Expected<DiffResult> GitRepo::diff(DiffTarget target, const std::filesystem::path& file) const
{
    std::string git_file = toGitPath(file);
    char* paths[]        = {git_file.data()};

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count   = 1;
    opts.flags            = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT;

    git_diff* raw = nullptr;
    int rc;
    if (target == DiffTarget::WorktreeVsIndex)
    {
        rc = git_diff_index_to_workdir(&raw, m_repo, nullptr, &opts);
    }
    else if (target == DiffTarget::WorktreeVsHead)
    {
        git_object* head_obj = nullptr;
        git_tree* head_tree  = nullptr;
        if (git_revparse_single(&head_obj, m_repo, "HEAD^{tree}") == 0)
            head_tree = reinterpret_cast<git_tree*>(head_obj);
        rc = git_diff_tree_to_workdir(&raw, m_repo, head_tree, &opts);
        if (head_tree)
            git_tree_free(head_tree);
    }
    else // IndexVsHead (unchanged)
    {
        git_object* head_obj = nullptr;
        git_tree* head_tree  = nullptr;
        if (git_revparse_single(&head_obj, m_repo, "HEAD^{tree}") == 0)
            head_tree = reinterpret_cast<git_tree*>(head_obj);
        rc = git_diff_tree_to_index(&raw, m_repo, head_tree, nullptr, &opts);
        if (head_tree)
            git_tree_free(head_tree);
    }
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff(raw, git_diff_free);
    return DiffEngine::parse(diff.get());
}

std::filesystem::path GitRepo::workdir() const
{
    const char* wd = git_repository_workdir(m_repo);
    return wd ? fromGitPath(wd) : std::filesystem::path{};
}

Expected<void> GitRepo::stage(const StageSelection& sel)
{
    git_index* index = nullptr;
    int rc           = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> idx_guard(index, git_index_free);

    if (isWholeFile(sel))
    {
        std::string p = toGitPath(sel.path);
        // If the file is gone from the worktree, stage its deletion.
        std::filesystem::path abs = sel.path.is_absolute() ? sel.path : workdir() / sel.path;
        if (!std::filesystem::exists(abs))
        {
            rc = git_index_remove_bypath(index, p.c_str());
        }
        else
        {
            rc = git_index_add_bypath(index, p.c_str());
        }
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        rc = git_index_write(index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        return {};
    }
    return applyPartial(sel, /*stage=*/true);
}

Expected<void> GitRepo::unstage(const StageSelection& sel)
{
    if (isWholeFile(sel))
    {
        // Reset the index entry for this path back to HEAD.
        git_object* head = nullptr;
        int rc           = git_revparse_single(&head, m_repo, "HEAD");
        if (rc < 0)
        {
            // Unborn branch: no HEAD, so unstaging == removing from index.
            git_index* index = nullptr;
            rc               = git_repository_index(&index, m_repo);
            if (rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> idx_guard(index, git_index_free);
            std::string p = toGitPath(sel.path);
            rc            = git_index_remove_bypath(index, p.c_str());
            if (rc < 0)
                return std::unexpected(lastGitError(rc));
            rc = git_index_write(index);
            if (rc < 0)
                return std::unexpected(lastGitError(rc));
            return {};
        }
        std::unique_ptr<git_object, decltype(&git_object_free)> head_guard(head, git_object_free);

        std::string p         = toGitPath(sel.path);
        char* paths[]         = {p.data()};
        git_strarray pathspec = {paths, 1};
        rc                    = git_reset_default(m_repo, head, &pathspec);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        return {};
    }
    return applyPartial(sel, /*stage=*/false);
}

Expected<void> GitRepo::applyPartial(const StageSelection& sel, bool stage)
{
    // 1. Get the diff that contains the selected hunk.
    DiffTarget target = stage ? DiffTarget::WorktreeVsIndex : DiffTarget::IndexVsHead;
    auto fileDiff     = diff(target, sel.path);
    if (!fileDiff)
        return std::unexpected(fileDiff.error());

    int hi = sel.hunkIndex.value_or(-1);
    if (hi < 0 || hi >= static_cast<int>(fileDiff->hunks.size()))
        return std::unexpected(GitError{-1, "hunk index out of range"});

    // 2. Build the patch buffer. Unstage reverses the index->HEAD diff.
    std::string patch = buildPatch(toGitPath(sel.path), fileDiff->hunks[hi], sel, /*reverse=*/!stage);

    // 3. Parse the buffer into a git_diff and apply it to the index.
    git_diff* raw = nullptr;
    int rc        = git_diff_from_buffer(&raw, patch.data(), patch.size());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    rc = git_apply(m_repo, raw, GIT_APPLY_LOCATION_INDEX, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::discard(const StageSelection& sel)
{
    std::string p = toGitPath(sel.path);

    if (isWholeFile(sel))
    {
        // Force-checkout the file from the index/HEAD, overwriting the worktree.
        git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
        opts.checkout_strategy    = GIT_CHECKOUT_FORCE;
        char* paths[]             = {p.data()};
        opts.paths.strings        = paths;
        opts.paths.count          = 1;
        int rc                    = git_checkout_head(m_repo, &opts);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        return {};
    }

    // Hunk/line: reverse-apply the worktree-vs-index patch to the WORKDIR.
    auto fileDiff = diff(DiffTarget::WorktreeVsIndex, sel.path);
    if (!fileDiff)
        return std::unexpected(fileDiff.error());
    int hi = sel.hunkIndex.value_or(-1);
    if (hi < 0 || hi >= static_cast<int>(fileDiff->hunks.size()))
        return std::unexpected(GitError{-1, "hunk index out of range"});

    std::string patch = buildPatch(p, fileDiff->hunks[hi], sel, /*reverse=*/true);

    git_diff* raw = nullptr;
    int rc        = git_diff_from_buffer(&raw, patch.data(), patch.size());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    rc = git_apply(m_repo, raw, GIT_APPLY_LOCATION_WORKDIR, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<std::string> GitRepo::commit(const CommitRequest& req)
{
    git_signature* sig = nullptr;
    int rc             = git_signature_default(&sig, m_repo); // reads user.name/user.email
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    git_index* index = nullptr;
    rc               = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> idx_guard(index, git_index_free);

    git_oid tree_oid;
    rc = git_index_write_tree(&tree_oid, index);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_tree* tree = nullptr;
    rc             = git_tree_lookup(&tree, m_repo, &tree_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);

    // Parent = current HEAD commit, if the branch is born.
    git_commit* parent = nullptr;
    git_oid parent_oid;
    git_commit* parents[1] = {nullptr};
    size_t parent_count    = 0;
    if (git_reference_name_to_id(&parent_oid, m_repo, "HEAD") == 0 && git_commit_lookup(&parent, m_repo, &parent_oid) == 0)
    {
        parents[0]   = parent;
        parent_count = 1;
    }

    git_oid commit_oid;
    rc = git_commit_create(&commit_oid, m_repo, "HEAD", sig, sig, nullptr, req.message.c_str(), tree, parent_count, parents);
    if (parent)
        git_commit_free(parent);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), &commit_oid);
    return std::string(buf);
}

Expected<void> GitRepo::resetIndexToHead()
{
    // Unborn HEAD: there is no commit to reset to, so clearing the index is the
    // equivalent "nothing staged" state.
    if (git_repository_head_unborn(m_repo) == 1)
    {
        git_index* index = nullptr;
        int rc           = git_repository_index(&index, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index, decltype(&git_index_free)> guard(index, git_index_free);
        git_index_clear(index);
        rc = git_index_write(index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        return {};
    }

    git_object* head = nullptr;
    int rc           = git_revparse_single(&head, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_object, decltype(&git_object_free)> head_guard(head, git_object_free);

    // MIXED resets the index to the target, leaving the working tree as-is.
    // Target is HEAD, so HEAD does not move — only the index is rewritten.
    rc = git_reset(m_repo, head, GIT_RESET_MIXED, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<std::vector<CommitNode>> GitRepo::log(unsigned limit) const
{
    git_revwalk* walk = nullptr;
    int rc            = git_revwalk_new(&walk, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

    rc = git_revwalk_push_head(walk);
    if (rc < 0)
    {
        git_revwalk_free(walk);
        // If the branch is unborn (no commits yet) or HEAD is missing,
        // treat as empty history rather than an error.
        int unborn = git_repository_head_unborn(m_repo);
        if (unborn > 0)
            return std::vector<CommitNode>{};
        return std::unexpected(lastGitError(rc));
    }

    std::vector<CommitNode> result;
    git_oid oid;
    unsigned count = 0;

    while ((limit == 0 || count < limit) && git_revwalk_next(&oid, walk) == 0)
    {
        git_commit* c = nullptr;
        if (git_commit_lookup(&c, m_repo, &oid) < 0)
            continue;

        CommitNode node;

        char hex[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(hex, sizeof(hex), &oid);
        node.oid = hex;

        const char* msg = git_commit_summary(c);
        node.summary    = msg ? msg : "";

        const git_signature* author = git_commit_author(c);
        node.author                 = author ? author->name : "";
        node.time                   = author ? author->when.time : 0;

        unsigned nparents = git_commit_parentcount(c);
        node.parents.reserve(nparents);
        for (unsigned i = 0; i < nparents; ++i)
        {
            const git_oid* pid = git_commit_parent_id(c, i);
            char phex[GIT_OID_SHA1_HEXSIZE + 1];
            git_oid_tostr(phex, sizeof(phex), pid);
            node.parents.push_back(phex);
        }

        git_commit_free(c);
        result.push_back(std::move(node));
        ++count;
    }

    git_revwalk_free(walk);
    return result;
}

namespace {
// Bit set indicating the submodule's index/working tree differs from its pin.
constexpr unsigned kSubmoduleDirtyMask =
    GIT_SUBMODULE_STATUS_INDEX_ADDED | GIT_SUBMODULE_STATUS_INDEX_DELETED |
    GIT_SUBMODULE_STATUS_INDEX_MODIFIED | GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED |
    GIT_SUBMODULE_STATUS_WD_WD_MODIFIED | GIT_SUBMODULE_STATUS_WD_MODIFIED |
    GIT_SUBMODULE_STATUS_WD_UNTRACKED | GIT_SUBMODULE_STATUS_WD_DELETED;

SubmoduleStatus classifySubmodule(unsigned flags)
{
    if (!(flags & GIT_SUBMODULE_STATUS_IN_WD))
        return SubmoduleStatus::Uninitialized;
    if (flags & kSubmoduleDirtyMask)
        return SubmoduleStatus::Dirty;
    return SubmoduleStatus::Clean;
}
} // namespace

Expected<std::vector<SubmoduleNode>> GitRepo::submoduleTree() const
{
    const std::filesystem::path wd = workdir();

    // Collect direct submodules (path/name/oid/status) inside the foreach, then
    // descend afterwards — opening child repositories inside the callback is
    // unsafe while libgit2 holds the submodule cache.
    struct Payload
    {
        std::vector<SubmoduleNode>* out;
        const std::filesystem::path* wd;
        git_repository* repo;
    };
    std::vector<SubmoduleNode> result;
    Payload payload{&result, &wd, m_repo};

    auto cb = [](git_submodule* sm, const char* name, void* pl) -> int
    {
        auto* p = static_cast<Payload*>(pl);

        SubmoduleNode node;
        node.name = name ? name : "";
        if (const char* rel = git_submodule_path(sm))
            node.path = *p->wd / fromGitPath(rel);

        unsigned flags = 0;
        if (git_submodule_status(&flags, p->repo, node.name.c_str(), GIT_SUBMODULE_IGNORE_UNSPECIFIED) == 0)
            node.status = classifySubmodule(flags);

        if (node.status != SubmoduleStatus::Uninitialized)
        {
            if (const git_oid* hid = git_submodule_head_id(sm))
            {
                char hex[GIT_OID_HEXSZ + 1];
                git_oid_tostr(hex, sizeof(hex), hid);
                node.shortOid.assign(hex, hex + 7);
            }
        }

        p->out->push_back(std::move(node));
        return 0;
    };

    const int rc = git_submodule_foreach(m_repo, cb, &payload);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    // Descend into each initialised submodule.
    for (auto& node : result)
    {
        if (node.status == SubmoduleStatus::Uninitialized)
            continue;
        auto child = GitRepo::open(node.path);
        if (!child)
            continue; // a broken child degrades to no children, not a fatal error
        if (auto sub = child->submoduleTree())
            node.children = std::move(*sub);
    }

    return result;
}

Expected<std::vector<BranchInfo>> GitRepo::branches() const
{
    std::vector<BranchInfo> result;
    const std::map<std::string, std::string> wtPaths = worktreeBranchPaths(m_repo);

    // Enumerate one branch scope (local or remote-tracking), appending to result.
    auto collect = [&](git_branch_t scope, BranchKind kind) -> Expected<void> {
        git_branch_iterator* it = nullptr;
        int rc = git_branch_iterator_new(&it, m_repo, scope);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_branch_iterator, decltype(&git_branch_iterator_free)> guard(it, git_branch_iterator_free);

        git_reference* ref = nullptr;
        git_branch_t   br_type;
        while ((rc = git_branch_next(&ref, &br_type, it)) == 0)
        {
            std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);
            const char* name = nullptr;
            if (git_branch_name(&name, ref) != 0 || !name)
                continue;
            BranchInfo info;
            info.name   = name;
            info.kind   = kind;
            info.isHead = kind == BranchKind::Local && git_branch_is_head(ref) == 1;
            if (kind == BranchKind::Local)
            {
                git_reference* up = nullptr;
                if (git_branch_upstream(&up, ref) == 0 && up)
                {
                    const char* up_name = nullptr;
                    if (git_branch_name(&up_name, up) == 0 && up_name)
                        info.upstream = up_name;
                    git_reference_free(up);
                }
                if (const auto wtIt = wtPaths.find(info.name); wtIt != wtPaths.end())
                    info.worktreePath = wtIt->second;
            }
            result.push_back(std::move(info));
        }
        if (rc != GIT_ITEROVER)
            return std::unexpected(lastGitError(rc));
        return {};
    };

    if (auto r = collect(GIT_BRANCH_LOCAL, BranchKind::Local); !r)
        return std::unexpected(r.error());
    if (auto r = collect(GIT_BRANCH_REMOTE, BranchKind::RemoteTracking); !r)
        return std::unexpected(r.error());
    return result;
}

Expected<HeadState> GitRepo::head() const
{
    HeadState st;
    if (git_repository_head_unborn(m_repo) == 1)
    {
        st.unborn = true;
        return st;
    }
    git_reference* ref = nullptr;
    int rc             = git_repository_head(&ref, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> guard(ref, git_reference_free);

    git_oid oid;
    if (git_reference_name_to_id(&oid, m_repo, "HEAD") == 0)
    {
        char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
        git_oid_tostr(hex, sizeof(hex), &oid);
        st.oid = hex;
    }
    st.detached = git_repository_head_detached(m_repo) == 1;
    if (!st.detached)
    {
        const char* sh = git_reference_shorthand(ref);
        st.branch      = sh ? sh : "";
    }
    return st;
}

Expected<void> GitRepo::createBranch(std::string name, std::string fromOid)
{
    int valid = 0;
    if (git_branch_name_is_valid(&valid, name.c_str()) < 0 || valid == 0)
        return std::unexpected(GitError{-1, "invalid branch name"});

    git_commit* target = nullptr;
    int rc;
    if (fromOid.empty())
    {
        git_oid head_oid;
        rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        rc = git_commit_lookup(&target, m_repo, &head_oid);
    }
    else
    {
        git_oid oid;
        rc = git_oid_fromstr(&oid, fromOid.c_str());
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        rc = git_commit_lookup(&target, m_repo, &oid);
    }
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> target_guard(target, git_commit_free);

    git_reference* new_ref = nullptr;
    rc = git_branch_create(&new_ref, m_repo, name.c_str(), target, /*force=*/0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_reference_free(new_ref);
    return {};
}

Expected<void> GitRepo::safeSwitch(const git_oid& targetCommit, const std::string& refToSet)
{
    // 1. Detect dirty working tree.
    auto st = status();
    if (!st)
        return std::unexpected(st.error());
    const bool dirty = !st->empty();

    // 2. Auto-stash if dirty.
    git_oid stash_oid;
    bool stashed = false;
    if (dirty)
    {
        git_signature* sig = nullptr;
        if (git_signature_default(&sig, m_repo) < 0)
            git_signature_now(&sig, "GitTide", "gittide@localhost");
        if (!sig)
            return std::unexpected(GitError{-1, "could not build a signature for the auto-stash"});
        std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

        int rc = git_stash_save(&stash_oid, m_repo, sig, "gittide: auto-stash on switch",
                                GIT_STASH_INCLUDE_UNTRACKED);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        stashed = true;
    }

    // 3. Checkout the target commit tree.
    git_commit* commit = nullptr;
    int rc             = git_commit_lookup(&commit, m_repo, &targetCommit);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy    = GIT_CHECKOUT_SAFE;
    rc = git_checkout_tree(m_repo, reinterpret_cast<const git_object*>(commit), &opts);
    if (rc < 0)
        // On failure here the auto-stash remains; HEAD is unchanged so the
        // user's changes are recoverable via the stash.
        return std::unexpected(lastGitError(rc));

    // 4. Update HEAD.
    if (refToSet.empty())
        rc = git_repository_set_head_detached(m_repo, &targetCommit);
    else
        rc = git_repository_set_head(m_repo, refToSet.c_str());
    if (rc < 0)
    {
        if (stashed)
            return std::unexpected(
                GitError{rc, "Failed to update HEAD; your uncommitted changes are saved in the stash"});
        return std::unexpected(lastGitError(rc));
    }

    // 5. Re-apply the stash if we created one.
    if (stashed)
    {
        git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
        rc = git_stash_pop(m_repo, 0, &aopts);
        if (rc < 0)
        {
            // Stash is intentionally preserved — the caller can inspect it.
            return std::unexpected(GitError{rc,
                "Switched branch, but your changes conflict and are kept in the stash"});
        }
    }
    return {};
}

Expected<void> GitRepo::checkoutBranch(std::string name)
{
    // Resolve the branch short-name to a full ref and OID.
    git_reference* ref = nullptr;
    int rc             = git_branch_lookup(&ref, m_repo, name.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);

    git_oid oid;
    rc = git_reference_name_to_id(&oid, m_repo, git_reference_name(ref));
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    return safeSwitch(oid, std::string("refs/heads/") + name);
}

Expected<void> GitRepo::checkoutRemoteBranch(std::string remoteShorthand)
{
    // Derive the local branch name by stripping the leading "<remote>/" segment.
    // Remote names cannot contain '/', so the first slash is the boundary; the
    // branch portion may itself contain slashes (e.g. "origin/feature/foo").
    const auto slash = remoteShorthand.find('/');
    if (slash == std::string::npos || slash + 1 >= remoteShorthand.size())
        return std::unexpected(GitError{-1, "not a remote-tracking branch: " + remoteShorthand});
    const std::string localName = remoteShorthand.substr(slash + 1);

    // DWIM: an existing local branch of that name is simply switched to.
    git_reference* existing = nullptr;
    if (git_branch_lookup(&existing, m_repo, localName.c_str(), GIT_BRANCH_LOCAL) == 0)
    {
        git_reference_free(existing);
        return checkoutBranch(localName);
    }

    // Resolve the remote-tracking ref to its commit.
    git_reference* remoteRef = nullptr;
    int rc = git_branch_lookup(&remoteRef, m_repo, remoteShorthand.c_str(), GIT_BRANCH_REMOTE);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> remote_guard(remoteRef, git_reference_free);

    git_oid oid;
    rc = git_reference_name_to_id(&oid, m_repo, git_reference_name(remoteRef));
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_commit* target = nullptr;
    rc = git_commit_lookup(&target, m_repo, &oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> target_guard(target, git_commit_free);

    // Create the local branch and set its upstream to the remote-tracking ref so
    // ahead/behind and pull/push resolve without extra configuration.
    git_reference* localRef = nullptr;
    rc = git_branch_create(&localRef, m_repo, localName.c_str(), target, /*force=*/0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> local_guard(localRef, git_reference_free);

    rc = git_branch_set_upstream(localRef, remoteShorthand.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    return safeSwitch(oid, std::string("refs/heads/") + localName);
}

Expected<void> GitRepo::checkoutCommit(std::string oid)
{
    git_oid target;
    int rc = git_oid_fromstr(&target, oid.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return safeSwitch(target, /*refToSet=*/"");
}

Expected<void> GitRepo::deleteBranch(std::string name, bool force)
{
    git_reference* ref = nullptr;
    int rc             = git_branch_lookup(&ref, m_repo, name.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> guard(ref, git_reference_free);

    if (git_branch_is_head(ref) == 1)
        return std::unexpected(GitError{-1, "cannot delete the current branch"});

    if (!force)
    {
        git_oid branch_oid, head_oid;
        if (git_reference_name_to_id(&branch_oid, m_repo, git_reference_name(ref)) == 0
            && git_reference_name_to_id(&head_oid, m_repo, "HEAD") == 0)
        {
            // merged == branch tip is an ancestor of (or equal to) HEAD.
            int merged = git_oid_equal(&branch_oid, &head_oid)
                         || git_graph_descendant_of(m_repo, &head_oid, &branch_oid) == 1;
            if (!merged)
                return std::unexpected(GitError{-1, "branch is not fully merged"});
        }
    }

    rc = git_branch_delete(ref);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::renameBranch(std::string oldName, std::string newName, bool force)
{
    int valid = 0;
    if (git_branch_name_is_valid(&valid, newName.c_str()) < 0 || valid == 0)
        return std::unexpected(GitError{-1, "invalid branch name"});

    git_reference* ref = nullptr;
    int rc             = git_branch_lookup(&ref, m_repo, oldName.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> guard(ref, git_reference_free);

    git_reference* moved = nullptr;
    rc = git_branch_move(&moved, ref, newName.c_str(), force ? 1 : 0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_reference_free(moved);
    return {};
}

Expected<void> GitRepo::commitTrees(const std::string& oidHex, git_tree** outTree, git_tree** outParentTree) const
{
    *outTree       = nullptr;
    *outParentTree = nullptr;

    git_oid oid;
    int rc = git_oid_fromstr(&oid, oidHex.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_commit* commit = nullptr;
    rc                 = git_commit_lookup(&commit, m_repo, &oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    rc = git_commit_tree(outTree, commit);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    if (git_commit_parentcount(commit) > 0)
    {
        git_commit* parent = nullptr;
        if (git_commit_parent(&parent, commit, 0) == 0)
        {
            std::unique_ptr<git_commit, decltype(&git_commit_free)> parent_guard(parent, git_commit_free);
            rc = git_commit_tree(outParentTree, parent);
            if (rc < 0)
            {
                git_tree_free(*outTree);
                *outTree = nullptr;
                return std::unexpected(lastGitError(rc));
            }
        }
    }
    return {};
}

Expected<std::vector<FileStatus>> GitRepo::commitFiles(std::string oid) const
{
    git_tree* tree       = nullptr;
    git_tree* parentTree = nullptr;
    if (auto r = commitTrees(oid, &tree, &parentTree); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> parent_guard(parentTree, git_tree_free);

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, parentTree, tree, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    std::vector<FileStatus> result;
    size_t n = git_diff_num_deltas(raw);
    result.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        const git_diff_delta* d = git_diff_get_delta(raw, i);
        StatusFlag flag         = StatusFlag::IndexModified;
        const char* path        = d->new_file.path;
        switch (d->status)
        {
            case GIT_DELTA_ADDED:
                flag = StatusFlag::IndexNew;
                break;
            case GIT_DELTA_DELETED:
                flag = StatusFlag::IndexDeleted;
                path = d->old_file.path;
                break;
            default: // MODIFIED, RENAMED, COPIED, TYPECHANGE → show as modified
                flag = StatusFlag::IndexModified;
                break;
        }
        if (path)
            result.push_back(FileStatus{fromGitPath(path), flag});
    }
    return result;
}

Expected<DiffResult> GitRepo::commitDiff(std::string oid, const std::filesystem::path& file) const
{
    git_tree* tree       = nullptr;
    git_tree* parentTree = nullptr;
    if (auto r = commitTrees(oid, &tree, &parentTree); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> parent_guard(parentTree, git_tree_free);

    std::string git_file = toGitPath(file);
    char* paths[]        = {git_file.data()};
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count   = 1;

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, parentTree, tree, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);
    return DiffEngine::parse(diff_guard.get());
}

Expected<SyncStatus> GitRepo::syncStatus() const
{
    SyncStatus out;

    git_reference* head = nullptr;
    int rc = git_repository_head(&head, m_repo);
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND)
        return out; // unborn => no upstream
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);

    if (git_reference_is_branch(head) != 1)
        return out; // detached HEAD => no upstream

    git_reference* upstream = nullptr;
    rc = git_branch_upstream(&upstream, head);
    if (rc == GIT_ENOTFOUND)
        return out; // no upstream configured
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> up_guard(upstream, git_reference_free);

    const git_oid* localOid    = git_reference_target(head);
    const git_oid* upstreamOid = git_reference_target(upstream);
    if (!localOid || !upstreamOid)
        return out;

    size_t ahead = 0, behind = 0;
    rc = git_graph_ahead_behind(&ahead, &behind, m_repo, localOid, upstreamOid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    out.hasUpstream  = true;
    out.ahead        = static_cast<int>(ahead);
    out.behind       = static_cast<int>(behind);
    const char* up   = git_reference_shorthand(upstream);
    out.upstreamName = up ? up : "";

    git_buf remote_buf = GIT_BUF_INIT;
    if (git_branch_remote_name(&remote_buf, m_repo, git_reference_name(upstream)) == 0)
        out.remoteName = std::string(remote_buf.ptr, remote_buf.size);
    git_buf_dispose(&remote_buf);

    return out;
}

Expected<PullStrategy> GitRepo::pullStrategy() const
{
    git_config* cfg = nullptr;
    int rc = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    int rebase = 0;
    rc = git_config_get_bool(&rebase, cfg, "pull.rebase");
    if (rc == GIT_ENOTFOUND)
        return PullStrategy::FastForwardOnly;
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return rebase ? PullStrategy::Rebase : PullStrategy::FastForwardOnly;
}

Expected<void> GitRepo::setPullStrategy(PullStrategy strategy)
{
    git_config* cfg = nullptr;
    int rc = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    rc = git_config_set_bool(cfg, "pull.rebase", strategy == PullStrategy::Rebase ? 1 : 0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::fetch(std::string remoteName, Credentials cred, ProgressCallback cb)
{
    git_remote* raw = nullptr;
    int rc          = git_remote_lookup(&raw, m_repo, remoteName.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_remote, decltype(&git_remote_free)> remote(raw, git_remote_free);

    CbPayload pl{&cb, &cred};
    git_fetch_options opts           = GIT_FETCH_OPTIONS_INIT;
    opts.callbacks.transfer_progress = transferProgressTrampoline;
    opts.callbacks.credentials       = credentialTrampoline;
    opts.callbacks.payload           = &pl;

    rc = git_remote_fetch(remote.get(), nullptr, &opts, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::pull(Credentials cred, ProgressCallback cb)
{
    // Resolve upstream + remote name from current branch.
    auto st = syncStatus();
    if (!st)
        return std::unexpected(st.error());
    if (!st->hasUpstream)
        return std::unexpected(GitError{-1, "current branch has no upstream"});

    auto fr = fetch(st->remoteName, cred, cb);
    if (!fr)
        return std::unexpected(fr.error());

    // Recompute after fetch; the upstream ref now points at the fetched tip.
    git_reference* head = nullptr;
    int rc = git_repository_head(&head, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);

    git_reference* upstream = nullptr;
    rc = git_branch_upstream(&upstream, head);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> up_guard(upstream, git_reference_free);

    git_annotated_commit* upstream_ac = nullptr;
    rc = git_annotated_commit_from_ref(&upstream_ac, m_repo, upstream);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_annotated_commit, decltype(&git_annotated_commit_free)> ac_guard(upstream_ac, git_annotated_commit_free);

    auto strat = pullStrategy();
    if (!strat)
        return std::unexpected(strat.error());

    if (*strat == PullStrategy::FastForwardOnly)
    {
        git_merge_analysis_t analysis = GIT_MERGE_ANALYSIS_NONE;
        git_merge_preference_t pref   = GIT_MERGE_PREFERENCE_NONE;
        const git_annotated_commit* heads[] = {upstream_ac};
        rc = git_merge_analysis(&analysis, &pref, m_repo, heads, 1);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE)
            return {};
        if (!(analysis & GIT_MERGE_ANALYSIS_FASTFORWARD))
            return std::unexpected(GitError{-1, "cannot fast-forward: branch has diverged"});

        // Move HEAD's branch ref to the upstream OID and checkout.
        const git_oid* target = git_annotated_commit_id(upstream_ac);
        git_object* target_obj = nullptr;
        rc = git_object_lookup(&target_obj, m_repo, target, GIT_OBJECT_COMMIT);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_object, decltype(&git_object_free)> obj_guard(target_obj, git_object_free);

        git_checkout_options co = GIT_CHECKOUT_OPTIONS_INIT;
        co.checkout_strategy    = GIT_CHECKOUT_SAFE;
        rc = git_checkout_tree(m_repo, target_obj, &co);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        git_reference* new_ref = nullptr;
        rc = git_reference_set_target(&new_ref, head, target, "pull: fast-forward");
        if (new_ref)
            git_reference_free(new_ref);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        return {};
    }

    // Rebase local commits onto the upstream.
    git_rebase* rebase = nullptr;
    git_rebase_options ropts = GIT_REBASE_OPTIONS_INIT;
    rc = git_rebase_init(&rebase, m_repo, /*branch=*/nullptr, upstream_ac, /*onto=*/nullptr, &ropts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rebase_guard(rebase, git_rebase_free);

    git_rebase_operation* op = nullptr;
    while ((rc = git_rebase_next(&op, rebase)) == 0)
    {
        git_index* idx = nullptr;
        if (git_repository_index(&idx, m_repo) == 0)
        {
            bool conflicts = git_index_has_conflicts(idx) > 0;
            git_index_free(idx);
            if (conflicts)
            {
                git_rebase_abort(rebase);
                return std::unexpected(GitError{-1, "pull rebase hit conflicts; resolve via CLI"});
            }
        }
        git_oid commit_id;
        git_signature* sig = nullptr;
        if (git_signature_default(&sig, m_repo) < 0)
        {
            git_rebase_abort(rebase);
            return std::unexpected(GitError{-1, "no committer identity (set user.name/user.email)"});
        }
        rc = git_rebase_commit(&commit_id, rebase, nullptr, sig, nullptr, nullptr);
        git_signature_free(sig);
        if (rc < 0 && rc != GIT_EAPPLIED)
        {
            git_rebase_abort(rebase);
            return std::unexpected(lastGitError(rc));
        }
    }
    if (rc != GIT_ITEROVER)
    {
        git_rebase_abort(rebase);
        return std::unexpected(lastGitError(rc));
    }
    rc = git_rebase_finish(rebase, nullptr);
    if (rc < 0)
    {
        git_rebase_abort(rebase);
        return std::unexpected(lastGitError(rc));
    }
    return {};
}

Expected<void> GitRepo::push(std::string remoteName, std::string branch, bool setUpstream,
                             Credentials cred, ProgressCallback cb)
{
    git_remote* raw = nullptr;
    int rc = git_remote_lookup(&raw, m_repo, remoteName.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_remote, decltype(&git_remote_free)> remote(raw, git_remote_free);

    std::string ref     = "refs/heads/" + branch;
    std::string refspec = ref + ":" + ref;
    char* specs[]       = {refspec.data()};
    git_strarray arr    = {specs, 1};

    CbPayload pl{&cb, &cred};
    git_push_options opts      = GIT_PUSH_OPTIONS_INIT;
    opts.callbacks.credentials = credentialTrampoline;
    opts.callbacks.payload     = &pl;

    rc = git_remote_push(remote.get(), &arr, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    if (setUpstream)
    {
        git_reference* branch_ref = nullptr;
        rc = git_branch_lookup(&branch_ref, m_repo, branch.c_str(), GIT_BRANCH_LOCAL);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_reference, decltype(&git_reference_free)> br_guard(branch_ref, git_reference_free);
        std::string upstream = remoteName + "/" + branch;
        rc = git_branch_set_upstream(branch_ref, upstream.c_str());
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    return {};
}

} // namespace gittide
