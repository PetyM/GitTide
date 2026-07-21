#include "gittide/gitrepo.hpp"

#include <fstream>
#include <git2.h>
#include <git2/annotated_commit.h>
#include <git2/branch.h>
#include <git2/checkout.h>
#include <git2/cherrypick.h>
#include <git2/commit.h>
#include <git2/config.h>
#include <git2/credential.h>
#include <git2/graph.h>
#include <git2/ignore.h>
#include <git2/merge.h>
#include <git2/rebase.h>
#include <git2/remote.h>
#include <git2/reset.h>
#include <git2/revwalk.h>
#include <git2/refs.h>
#include <git2/stash.h>
#include <git2/worktree.h>
#include <map>
#include <memory>
#include <optional>
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

// True if `p` (repo-relative, forward slashes) has an entry in the committed
// HEAD tree. False on an unborn branch (nothing committed yet).
bool pathInHead(git_repository* repo, const std::string& p)
{
    git_object* obj = nullptr;
    if (git_revparse_single(&obj, repo, "HEAD^{tree}") != 0)
        return false;
    std::unique_ptr<git_object, decltype(&git_object_free)> obj_guard(obj, git_object_free);

    git_tree_entry* entry = nullptr;
    int rc                = git_tree_entry_bypath(&entry, reinterpret_cast<git_tree*>(obj), p.c_str());
    if (rc == 0)
        git_tree_entry_free(entry);
    return rc == 0;
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
    if (s & GIT_STATUS_CONFLICTED)
        f |= StatusFlag::Conflicted;
    return f;
}

// Submodule working-tree dirtiness (uncommitted work *inside* the submodule),
// excluding the pointer-moved bit (WD_MODIFIED) which is a clean pointer change.
constexpr unsigned kSubmoduleWorkdirDirtyMask =
    GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED | GIT_SUBMODULE_STATUS_WD_WD_MODIFIED |
    GIT_SUBMODULE_STATUS_WD_UNTRACKED | GIT_SUBMODULE_STATUS_WD_DELETED;

// Submodule status bits for a changed gitlink at `path`: Submodule, plus
// SubmoduleDirty when the submodule's own working tree carries uncommitted work.
// Staging a submodule always records its HEAD (last commit), so dirtiness is a
// warning, never recorded into the superproject.
gittide::StatusFlag submoduleFlagsFor(git_repository* repo, const char* path)
{
    using gittide::StatusFlag;
    unsigned sf = 0;
    // git_submodule_status takes the submodule *name*; for the common case it
    // equals the path. If it can't be read, the gitlink is still a submodule.
    if (git_submodule_status(&sf, repo, path, GIT_SUBMODULE_IGNORE_UNSPECIFIED) != 0)
        return StatusFlag::Submodule;
    StatusFlag out = StatusFlag::Submodule;
    if (sf & kSubmoduleWorkdirDirtyMask)
        out |= StatusFlag::SubmoduleDirty;
    return out;
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

// Push side: libgit2 reports objects written / total as the pack uploads. Same
// (received, total) shape as fetch so the UI treats both uniformly.
int pushTransferProgressTrampoline(unsigned current, unsigned total, size_t /*bytes*/, void* payload)
{
    auto* pl = static_cast<CbPayload*>(payload);
    return (*pl->cb)(current, total) ? 0 : -1;
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
    case gittide::CredentialKind::SshKey:
        return git_credential_ssh_key_new(out, username_from_url ? username_from_url : "git",
                                          cred.sshPublicKeyPath.empty() ? nullptr : cred.sshPublicKeyPath.c_str(),
                                          cred.sshPrivateKeyPath.c_str(), cred.sshPassphrase.c_str());
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

Expected<GitRepo> GitRepo::clone(const std::string& url, const std::filesystem::path& dest, Credentials cred,
                                 ProgressCallback cb)
{
    CbPayload pl{&cb, &cred};
    git_clone_options opts                      = GIT_CLONE_OPTIONS_INIT;
    opts.fetch_opts.callbacks.transfer_progress = transferProgressTrampoline;
    opts.fetch_opts.callbacks.credentials       = credentialTrampoline;
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
        const git_diff_file* nf   = e->index_to_workdir ? &e->index_to_workdir->new_file
                                    : e->head_to_index  ? &e->head_to_index->new_file
                                                        : nullptr;
        const char* raw           = nf ? nf->path : nullptr;
        if (!raw)
            continue;
        StatusFlag flags = mapStatus(e->status);
        // A gitlink (mode 0160000) is a submodule pointer change; tag it (and its
        // working-tree dirtiness) so the UI can offer the tri-state pointer update.
        if (nf->mode == GIT_FILEMODE_COMMIT)
            flags |= submoduleFlagsFor(m_repo, raw);
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
    // RECURSE_UNTRACKED_DIRS so a file inside a brand-new untracked directory is
    // diffed individually — without it libgit2 collapses the whole new directory
    // into one untracked entry and a per-file pathspec matches nothing (status
    // already recurses, so the file shows as "U" but its diff came back empty).
    opts.flags = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT |
                 GIT_DIFF_RECURSE_UNTRACKED_DIRS;

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

Expected<WatchTargets> GitRepo::watchTargets() const
{
    namespace fs = std::filesystem;
    WatchTargets t;

    if (const char* gd = git_repository_path(m_repo))
        t.gitDir = fromGitPath(gd);

    // Enumerate the git dir's directory subtree: every directory under it is a
    // place where libgit2's atomic *.lock → rename rewrites land.
    std::error_code ec;
    if (!t.gitDir.empty() && fs::exists(t.gitDir, ec))
    {
        t.dirs.push_back(t.gitDir);
        for (auto it = fs::recursive_directory_iterator(t.gitDir, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec))
        {
            if (it->is_directory(ec))
                t.dirs.push_back(it->path());
        }
    }

    // Walk the working tree, watching each directory and pruning gitignored
    // subtrees (so node_modules / build never flood the watcher) and the .git dir.
    if (const char* wd = git_repository_workdir(m_repo))
    {
        t.workdir = fromGitPath(wd);
        t.dirs.push_back(t.workdir);

        std::error_code wec;
        auto       it  = fs::recursive_directory_iterator(t.workdir, fs::directory_options::skip_permission_denied, wec);
        const auto end = fs::recursive_directory_iterator();
        for (; !wec && it != end; it.increment(wec))
        {
            if (!it->is_directory(wec))
                continue;
            const fs::path& dir = it->path();
            if (dir.filename() == ".git")
            {
                it.disable_recursion_pending(); // the git dir is enumerated above
                continue;
            }
            // libgit2 ignore rules want a workdir-relative, forward-slash path; a
            // trailing slash makes a directory match a "build/"-style rule.
            const std::string rel = toGitPath(fs::relative(dir, t.workdir, wec)) + "/";
            int               ignored = 0;
            if (git_ignore_path_is_ignored(&ignored, m_repo, rel.c_str()) == 0 && ignored)
            {
                it.disable_recursion_pending();
                continue;
            }
            t.dirs.push_back(dir);
        }
    }

    return t;
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
        // A file with no committed (HEAD) version is "new" (untracked or just
        // staged): there is nothing to restore, so discarding means removing it.
        // Drop any staged entry and delete it from the worktree.
        if (!pathInHead(m_repo, p))
        {
            git_index* index = nullptr;
            int rc           = git_repository_index(&index, m_repo);
            if (rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> idx_guard(index, git_index_free);
            git_index_remove_bypath(index, p.c_str()); // no-op if not staged
            rc = git_index_write(index);
            if (rc < 0)
                return std::unexpected(lastGitError(rc));

            std::filesystem::path abs = sel.path.is_absolute() ? sel.path : workdir() / sel.path;
            std::error_code       ec;
            std::filesystem::remove(abs, ec);
            if (ec)
                return std::unexpected(GitError{-1, "failed to delete '" + p + "': " + ec.message()});
            return {};
        }

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

Expected<void> GitRepo::discardAll()
{
    // 1. Reset tracked changes (staged + unstaged) back to HEAD.
    if (git_repository_head_unborn(m_repo) == 1)
    {
        // No commit yet: clear the index so nothing stays staged.
        git_index* index = nullptr;
        int rc           = git_repository_index(&index, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index, decltype(&git_index_free)> idx_guard(index, git_index_free);
        git_index_clear(index);
        rc = git_index_write(index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    else
    {
        git_object* head = nullptr;
        int rc           = git_revparse_single(&head, m_repo, "HEAD");
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_object, decltype(&git_object_free)> head_guard(head, git_object_free);

        rc = git_reset(m_repo, head, GIT_RESET_HARD, nullptr);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }

    // 2. Delete untracked files (git clean -fd equivalent). Hard reset leaves
    //    untracked files in place, so enumerate and remove them explicitly.
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show               = GIT_STATUS_SHOW_WORKDIR_ONLY;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED;

    git_status_list* raw = nullptr;
    int rc               = git_status_list_new(&raw, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_status_list, decltype(&git_status_list_free)> list(raw, git_status_list_free);

    const std::filesystem::path wd = workdir();
    size_t n                       = git_status_list_entrycount(list.get());
    for (size_t i = 0; i < n; ++i)
    {
        const git_status_entry* e = git_status_byindex(list.get(), i);
        if (!(e->status & GIT_STATUS_WT_NEW))
            continue;
        const git_diff_file* nf = e->index_to_workdir ? &e->index_to_workdir->new_file : nullptr;
        if (!nf || !nf->path)
            continue;
        std::error_code ec;
        std::filesystem::remove_all(wd / fromGitPath(nf->path), ec); // best-effort; handles dirs too
    }
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

Expected<std::string> GitRepo::rewordHead(std::string newMessage)
{
    git_reference* head = nullptr;
    int rc              = git_repository_head(&head, m_repo);
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND)
        return std::unexpected(GitError{-1, "cannot reword: no commit on this branch"});
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);

    if (git_reference_is_branch(head) != 1)
        return std::unexpected(GitError{-1, "cannot reword a detached HEAD"});

    git_oid head_oid;
    rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* commit = nullptr;
    rc                 = git_commit_lookup(&commit, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    // Refresh committer from config; keep author (nullptr) and tree (nullptr).
    git_signature* committer = nullptr;
    rc                       = git_signature_default(&committer, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(committer, git_signature_free);

    git_oid new_oid;
    rc = git_commit_amend(&new_oid, commit, "HEAD", /*author=*/nullptr, committer,
                          /*encoding=*/nullptr, newMessage.c_str(), /*tree=*/nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), &new_oid);
    return std::string(buf);
}

Expected<void> GitRepo::undoLastCommit()
{
    // Refuse while any other operation owns the worktree (merge / plain rebase /
    // cherry-pick), or while our manual interactive engine is mid-flight — moving
    // HEAD underneath them would corrupt their state (mutual exclusion, D33).
    if (git_repository_state(m_repo) != GIT_REPOSITORY_STATE_NONE || interactiveRebaseInProgress())
        return std::unexpected(GitError{-1, "cannot undo: another operation is in progress"});

    git_reference* head = nullptr;
    int rc              = git_repository_head(&head, m_repo);
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND)
        return std::unexpected(GitError{-1, "cannot undo: no commit on this branch"});
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);

    if (git_reference_is_branch(head) != 1)
        return std::unexpected(GitError{-1, "cannot undo on a detached HEAD"});

    git_oid head_oid;
    rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* commit = nullptr;
    rc                 = git_commit_lookup(&commit, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    if (git_commit_parentcount(commit) == 0)
        return std::unexpected(GitError{-1, "cannot undo a root commit"});

    git_commit* parent = nullptr;
    rc                 = git_commit_parent(&parent, commit, 0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> parent_guard(parent, git_commit_free);

    // SOFT moves the branch ref to the parent but leaves the index and working
    // tree untouched, so the undone commit's changes remain staged.
    rc = git_reset(m_repo, reinterpret_cast<const git_object*>(parent), GIT_RESET_SOFT, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<std::string> GitRepo::commitMessage(std::string oidHex) const
{
    git_oid oid;
    int rc = git_oid_fromstr(&oid, oidHex.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* commit = nullptr;
    rc                 = git_commit_lookup(&commit, m_repo, &oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    const char* msg = git_commit_message(commit);
    return std::string(msg ? msg : "");
}

Expected<std::string> GitRepo::firstParent(std::string oidHex) const
{
    git_oid oid;
    int rc = git_oid_fromstr(&oid, oidHex.c_str());
    if (rc < 0)
        return std::unexpected(GitError{-1, "bad oid"});
    git_commit* c = nullptr;
    rc            = git_commit_lookup(&c, m_repo, &oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> cg(c, git_commit_free);
    if (git_commit_parentcount(c) == 0)
        return std::unexpected(GitError{-1, "cannot edit history from a root commit"});
    const git_oid* p = git_commit_parent_id(c, 0);
    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), p);
    return std::string(buf);
}

Expected<std::string> GitRepo::commitMerge(CommitRequest req)
{
    git_index* index = nullptr;
    int rc           = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> index_guard(index, git_index_free);

    if (git_index_has_conflicts(index))
        return std::unexpected(GitError{-1, "cannot commit: unresolved conflicts remain"});

    git_oid tree_oid;
    rc = git_index_write_tree(&tree_oid, index);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_tree* tree = nullptr;
    rc             = git_tree_lookup(&tree, m_repo, &tree_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);

    // Parents: current HEAD, then every MERGE_HEAD.
    std::vector<git_commit*> parents;
    auto free_parents = [&]() { for (auto* p : parents) git_commit_free(p); };

    git_oid head_oid;
    rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* head_commit = nullptr;
    rc                       = git_commit_lookup(&head_commit, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    parents.push_back(head_commit);

    struct CbData
    {
        GitRepo* self;
        std::vector<git_commit*>* parents;
        int rc;
    };
    CbData cb{this, &parents, 0};
    git_repository_mergehead_foreach(
        m_repo,
        [](const git_oid* oid, void* payload) -> int {
            auto* d = static_cast<CbData*>(payload);
            git_commit* c = nullptr;
            int r         = git_commit_lookup(&c, d->self->m_repo, oid);
            if (r < 0)
            {
                d->rc = r;
                return r;
            }
            d->parents->push_back(c);
            return 0;
        },
        &cb);
    if (cb.rc < 0)
    {
        free_parents();
        return std::unexpected(lastGitError(cb.rc));
    }

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
    {
        if (sig)
        {
            git_signature_free(sig);
            sig = nullptr;
        }
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    }
    if (!sig)
    {
        free_parents();
        return std::unexpected(GitError{-1, "no signature for merge commit"});
    }
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    git_oid commit_oid;
    rc = git_commit_create(&commit_oid, m_repo, "HEAD", sig, sig, nullptr, req.message.c_str(), tree,
                           parents.size(), const_cast<git_commit* const*>(parents.data()));
    free_parents();
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_repository_state_cleanup(m_repo); // clears MERGE_HEAD / MERGE_MSG

    char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(hex, sizeof(hex), &commit_oid);
    return std::string(hex);
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

namespace {
// Fill a CommitNode from a looked-up commit object (shared by log/logAllRefs).
// Returns std::nullopt when git_commit_lookup fails (unresolvable / corrupt OID).
std::optional<gittide::CommitNode> nodeFromCommit(git_repository* repo, const git_oid& oid)
{
    git_commit* c = nullptr;
    if (git_commit_lookup(&c, repo, &oid) < 0)
        return std::nullopt;

    gittide::CommitNode node;
    char hex[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(hex, sizeof(hex), &oid);
    node.oid = hex;

    const char* msg = git_commit_summary(c);
    node.summary    = msg ? msg : "";
    const git_signature* author = git_commit_author(c);
    node.author = author && author->name ? author->name : "";
    node.email  = author && author->email ? author->email : "";
    node.time   = author ? author->when.time : 0;

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
    return node;
}
} // namespace

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
        auto node = nodeFromCommit(m_repo, oid);
        if (!node)
            continue;
        result.push_back(std::move(*node));
        ++count;
    }

    git_revwalk_free(walk);
    return result;
}

Expected<std::vector<CommitNode>> GitRepo::logAllRefs(unsigned limit) const
{
    git_revwalk* walk = nullptr;
    int rc            = git_revwalk_new(&walk, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

    // Push every ref. Missing globs (e.g. no remotes) are not fatal — a repo
    // with no matching refs simply contributes nothing.
    git_revwalk_push_glob(walk, "refs/heads/*");
    git_revwalk_push_glob(walk, "refs/remotes/*");
    git_revwalk_push_glob(walk, "refs/tags/*");

    std::vector<CommitNode> result;
    git_oid oid;
    unsigned count = 0;
    while ((limit == 0 || count < limit) && git_revwalk_next(&oid, walk) == 0)
    {
        auto node = nodeFromCommit(m_repo, oid);
        if (!node)
            continue;
        result.push_back(std::move(*node));
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
        std::vector<std::string>*   pinnedFull; // full 40-hex pinned oid per node, "" if uninitialised
        const std::filesystem::path* wd;
        git_repository* repo;
    };
    std::vector<SubmoduleNode> result;
    std::vector<std::string>   pinnedFull;
    Payload payload{&result, &pinnedFull, &wd, m_repo};

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

        std::string pinnedHex;
        if (node.status != SubmoduleStatus::Uninitialized)
        {
            if (const git_oid* hid = git_submodule_head_id(sm))
            {
                char hex[GIT_OID_HEXSZ + 1];
                git_oid_tostr(hex, sizeof(hex), hid);
                node.shortOid.assign(hex, hex + 7);
                pinnedHex.assign(hex, hex + GIT_OID_HEXSZ);
            }
        }

        p->out->push_back(std::move(node));
        p->pinnedFull->push_back(std::move(pinnedHex));
        return 0;
    };

    const int rc = git_submodule_foreach(m_repo, cb, &payload);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    // Descend into each initialised submodule; fill per-submodule detail from its
    // own repository (branch/dirty/current-oid + ahead/behind of current vs pin).
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        SubmoduleNode& node = result[i];
        if (node.status == SubmoduleStatus::Uninitialized)
            continue;
        auto child = GitRepo::open(node.path);
        if (!child)
            continue; // a broken child degrades to no detail, not a fatal error

        if (auto hs = child->head())
        {
            node.branch       = hs->branch;
            node.detached     = hs->detached;
            node.headShortOid = hs->oid.size() >= 7 ? hs->oid.substr(0, 7) : hs->oid;
            if (!hs->oid.empty() && !pinnedFull[i].empty())
            {
                // current HEAD ahead/behind of the pinned commit, on the submodule repo.
                if (auto ab = child->aheadBehind(hs->oid, pinnedFull[i]))
                {
                    node.ahead  = ab->first;
                    node.behind = ab->second;
                }
                // pinned commit absent (shallow) → aheadBehind fails → leave 0/0.
            }
        }
        if (auto st = child->status())
            node.dirtyCount = static_cast<int>(st->size());

        if (auto sub = child->submoduleTree())
            node.children = std::move(*sub);
    }

    return result;
}

Expected<void> GitRepo::deinitSubmodule(std::filesystem::path path)
{
    // Verify it is actually a submodule (gives a clear error otherwise).
    git_submodule* sm = nullptr;
    int rc            = git_submodule_lookup(&sm, m_repo, toGitPath(path).c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_submodule_free(sm);

    // Emulate `submodule deinit`: remove the working-tree contents but
    // preserve the gitlink (.git file) so reinit can re-checkout rather than
    // re-clone. The gitlink and the modules directory in the superproject's
    // .git stay intact; only the checked-out source files are removed.
    const std::filesystem::path wd      = workdir() / path;
    const std::filesystem::path gitlink = wd / ".git";
    std::error_code iter_ec;
    std::filesystem::directory_iterator it(wd, iter_ec);
    if (iter_ec)
        return std::unexpected(GitError{-1, "failed to open submodule working dir: " + iter_ec.message()});
    for (const auto& entry : it)
    {
        if (entry.path() == gitlink)
            continue;
        std::error_code rm_ec;
        std::filesystem::remove_all(entry.path(), rm_ec);
        if (rm_ec)
            return std::unexpected(GitError{-1, "failed to clear submodule working dir: " + rm_ec.message()});
    }
    return {};
}

Expected<void> GitRepo::reinitSubmodule(std::filesystem::path path)
{
    git_submodule* sm = nullptr;
    int rc            = git_submodule_lookup(&sm, m_repo, toGitPath(path).c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_submodule, decltype(&git_submodule_free)> sm_guard(sm, git_submodule_free);

    git_submodule_update_options opts        = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
    opts.checkout_opts.checkout_strategy     = GIT_CHECKOUT_FORCE;
    rc                                       = git_submodule_update(sm, /*init=*/1, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::updateSubmodules()
{
    // Collect direct submodule names first; updating inside git_submodule_foreach
    // is unsafe while libgit2 holds the submodule cache (see submoduleTree).
    std::vector<std::string> names;
    auto cb = [](git_submodule*, const char* name, void* pl) -> int
    {
        static_cast<std::vector<std::string>*>(pl)->emplace_back(name ? name : "");
        return 0;
    };
    int rc = git_submodule_foreach(m_repo, cb, &names);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    for (const std::string& name : names)
    {
        git_submodule* sm = nullptr;
        rc                = git_submodule_lookup(&sm, m_repo, name.c_str());
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_submodule, decltype(&git_submodule_free)> guard(sm, git_submodule_free);

        git_submodule_update_options opts    = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
        opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
        rc                                   = git_submodule_update(sm, /*init=*/1, &opts);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    return {};
}

Expected<std::vector<RefTip>> GitRepo::refTips() const
{
    git_reference_iterator* it = nullptr;
    int rc = git_reference_iterator_new(&it, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    std::vector<RefTip> tips;
    git_reference* ref = nullptr;
    while (git_reference_next(&ref, it) == 0)
    {
        const char* name = git_reference_name(ref); // full, e.g. refs/heads/main
        std::string full = name ? name : "";

        // Peel to a commit; skip refs that don't resolve to one (e.g. annotated
        // tag of a tree, symbolic HEAD).
        git_object* obj = nullptr;
        if (git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT) == 0)
        {
            git_oid out;
            git_oid_cpy(&out, git_object_id(obj));
            char hex[GIT_OID_SHA1_HEXSIZE + 1];
            git_oid_tostr(hex, sizeof(hex), &out);
            git_object_free(obj);

            RefTip tip;
            tip.oid = hex;
            if (full.rfind("refs/heads/", 0) == 0)
            {
                tip.kind = RefTipKind::Branch;
                tip.name = full.substr(std::string("refs/heads/").size());
            }
            else if (full.rfind("refs/remotes/", 0) == 0)
            {
                tip.kind = RefTipKind::Remote;
                tip.name = full.substr(std::string("refs/remotes/").size());
                // Skip the synthetic origin/HEAD pointer.
                if (tip.name.size() >= 5 &&
                    tip.name.compare(tip.name.size() - 5, 5, "/HEAD") == 0)
                {
                    git_reference_free(ref);
                    ref = nullptr;
                    continue;
                }
            }
            else if (full.rfind("refs/tags/", 0) == 0)
            {
                tip.kind = RefTipKind::Tag;
                tip.name = full.substr(std::string("refs/tags/").size());
            }
            else
            {
                git_reference_free(ref);
                ref = nullptr;
                continue; // ignore refs/stash, notes, etc.
            }
            tips.push_back(std::move(tip));
        }
        git_reference_free(ref);
        ref = nullptr;
    }
    git_reference_iterator_free(it);
    return tips;
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
            // Peel to commit to get the tip OID (works for both local and remote).
            {
                git_object* obj = nullptr;
                if (git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT) == 0)
                {
                    char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
                    git_oid_tostr(hex, sizeof(hex), git_object_id(obj));
                    info.tipOid = hex;
                    git_object_free(obj);
                }
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

namespace {
// Parse the branch name out of a MERGE_MSG first line: "Merge branch 'feature/x'".
std::string parseMergedRef(const std::filesystem::path& gitdir)
{
    std::ifstream in(gitdir / "MERGE_MSG");
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    const auto a = line.find('\'');
    if (a == std::string::npos) return {};
    const auto b = line.find('\'', a + 1);
    if (b == std::string::npos) return {};
    return line.substr(a + 1, b - a - 1);
}
} // namespace

Expected<MergeState> GitRepo::mergeState() const
{
    MergeState st;
    st.inProgress = git_repository_state(m_repo) == GIT_REPOSITORY_STATE_MERGE;

    git_index* index = nullptr;
    int rc           = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> index_guard(index, git_index_free);

    if (git_index_has_conflicts(index))
    {
        git_index_conflict_iterator* it = nullptr;
        rc = git_index_conflict_iterator_new(&it, index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index_conflict_iterator,
                        decltype(&git_index_conflict_iterator_free)>
            it_guard(it, git_index_conflict_iterator_free);

        const git_index_entry *ancestor = nullptr, *our = nullptr, *their = nullptr;
        while (git_index_conflict_next(&ancestor, &our, &their, it) == 0)
        {
            const git_index_entry* e = our ? our : (their ? their : ancestor);
            if (!e || !e->path)
                continue;
            std::filesystem::path p = fromGitPath(e->path);
            st.conflictedPaths.push_back(p);
            // A gitlink (submodule) conflict: any side carrying the commit mode.
            const bool gitlink =
                (our && our->mode == GIT_FILEMODE_COMMIT) ||
                (their && their->mode == GIT_FILEMODE_COMMIT) ||
                (ancestor && ancestor->mode == GIT_FILEMODE_COMMIT);
            if (gitlink)
                st.conflictedSubmodules.push_back(p);
        }
    }

    if (st.inProgress)
    {
        const char* gp = git_repository_path(m_repo); // the .git dir
        if (gp)
            st.mergedRef = parseMergedRef(fromGitPath(gp));
    }
    return st;
}

std::string GitRepo::rebaseOntoName() const
{
    const char* gp = git_repository_path(m_repo); // the .git dir
    if (!gp)
        return {};
    namespace fs = std::filesystem;
    for (const char* sub : {"rebase-merge", "rebase-apply"})
    {
        fs::path f = fs::path(fromGitPath(gp)) / sub / "onto_name";
        std::ifstream in(f);
        if (in)
        {
            std::string line;
            std::getline(in, line);
            return line; // may be a ref shorthand or an oid; best-effort label
        }
    }
    return {};
}

// ── Interactive rebase state-dir helpers ─────────────────────────────────────

std::filesystem::path GitRepo::interactiveRebaseDir() const
{
    const char* gp = git_repository_path(m_repo); // the .git dir (trailing slash)
    if (!gp)
        return {};
    return std::filesystem::path(fromGitPath(gp)) / "gittide-rebase";
}

bool GitRepo::interactiveRebaseInProgress() const
{
    std::error_code ec;
    return std::filesystem::exists(interactiveRebaseDir() / "todo", ec);
}

static const char* actionToken(gittide::RebaseAction a)
{
    using A = gittide::RebaseAction;
    switch (a)
    {
        case A::Pick:   return "pick";
        case A::Reword: return "reword";
        case A::Squash: return "squash";
        case A::Fixup:  return "fixup";
        case A::Drop:   return "drop";
    }
    return "pick";
}

static gittide::RebaseAction tokenToAction(const std::string& t)
{
    using A = gittide::RebaseAction;
    if (t == "reword") return A::Reword;
    if (t == "squash") return A::Squash;
    if (t == "fixup")  return A::Fixup;
    if (t == "drop")   return A::Drop;
    return A::Pick;
}

Expected<void> GitRepo::initInteractiveState(const RebaseTodo& todo, const std::string& branch,
                                             const std::string& origHead)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = interactiveRebaseDir();
    fs::create_directories(dir, ec);
    if (ec)
        return std::unexpected(GitError{-1, "cannot create rebase state dir"});

    { std::ofstream(dir / "base")      << todo.base    << "\n"; }
    { std::ofstream(dir / "branch")    << branch       << "\n"; }
    { std::ofstream(dir / "orig-head") << origHead     << "\n"; }
    {
        std::ofstream todoFile(dir / "todo");
        for (const auto& e : todo.entries)
            todoFile << actionToken(e.action) << ' ' << e.oid << "\n";
    }
    { std::ofstream(dir / "done") << 0 << "\n"; }
    std::error_code rmEc;
    fs::remove(dir / "applied", rmEc); // ensure clean
    return {};
}

Expected<GitRepo::InteractiveState> GitRepo::loadInteractiveState() const
{
    namespace fs = std::filesystem;
    const fs::path dir = interactiveRebaseDir();
    std::error_code ec;
    if (!fs::exists(dir / "todo", ec))
        return std::unexpected(GitError{-1, "no interactive rebase in progress"});

    InteractiveState st;
    auto readLine = [&](const char* name) -> std::string {
        std::ifstream in(dir / name);
        std::string line;
        std::getline(in, line);
        return line;
    };
    st.todo.base = readLine("base");
    st.branch    = readLine("branch");
    st.origHead  = readLine("orig-head");
    {
        std::ifstream todoFile(dir / "todo");
        std::string line;
        while (std::getline(todoFile, line))
        {
            if (line.empty())
                continue;
            const auto sp = line.find(' ');
            if (sp == std::string::npos)
                continue;
            RebaseTodoEntry e;
            e.action = tokenToAction(line.substr(0, sp));
            e.oid    = line.substr(sp + 1);
            st.todo.entries.push_back(e);
        }
    }
    try { st.done = std::stoi(readLine("done")); } catch (...) { st.done = 0; }
    st.applied = fs::exists(dir / "applied", ec);
    return st;
}

Expected<void> GitRepo::setInteractiveProgress(int done, bool applied) const
{
    namespace fs = std::filesystem;
    const fs::path dir = interactiveRebaseDir();
    { std::ofstream(dir / "done") << done << "\n"; }
    std::error_code ec;
    if (applied)
        { std::ofstream(dir / "applied") << "1\n"; }
    else
        fs::remove(dir / "applied", ec);
    return {};
}

void GitRepo::clearInteractiveState() const
{
    std::error_code ec;
    std::filesystem::remove_all(interactiveRebaseDir(), ec);
}

RebaseState GitRepo::interactiveRebaseState() const
{
    RebaseState st;
    auto loaded = loadInteractiveState();
    if (!loaded)
        return st; // dir vanished mid-read → not in progress
    const InteractiveState& s = *loaded;
    st.inProgress  = true;
    st.interactive = true;

    const auto& entries = s.todo.entries;
    for (const auto& e : entries)
        if (e.action != RebaseAction::Drop)
            ++st.total;
    int committedKeep = 0;
    for (int i = 0; i < s.done && i < static_cast<int>(entries.size()); ++i)
        if (entries[i].action != RebaseAction::Drop)
            ++committedKeep;
    st.current = committedKeep + 1;
    if (st.current > st.total)
        st.current = st.total;

    const RebaseTodoEntry* cur =
        (s.done < static_cast<int>(entries.size())) ? &entries[s.done] : nullptr;
    if (cur)
    {
        git_oid o;
        if (git_oid_fromstr(&o, cur->oid.c_str()) == 0)
        {
            git_commit* c = nullptr;
            if (git_commit_lookup(&c, m_repo, &o) == 0)
            {
                std::unique_ptr<git_commit, decltype(&git_commit_free)> cg(c, git_commit_free);
                if (const char* sm = git_commit_summary(c))
                    st.stepSummary = sm;
            }
        }
    }

    // Conflicts — identical derivation to mergeState()/rebaseState().
    git_index* index = nullptr;
    if (git_repository_index(&index, m_repo) == 0)
    {
        std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
        if (git_index_has_conflicts(index))
        {
            git_index_conflict_iterator* it = nullptr;
            if (git_index_conflict_iterator_new(&it, index) == 0)
            {
                std::unique_ptr<git_index_conflict_iterator,
                                decltype(&git_index_conflict_iterator_free)>
                    itg(it, git_index_conflict_iterator_free);
                const git_index_entry *anc = nullptr, *our = nullptr, *their = nullptr;
                while (git_index_conflict_next(&anc, &our, &their, it) == 0)
                {
                    const git_index_entry* en = our ? our : (their ? their : anc);
                    if (!en || !en->path)
                        continue;
                    std::filesystem::path p = fromGitPath(en->path);
                    st.conflictedPaths.push_back(p);
                    const bool gitlink =
                        (our && our->mode == GIT_FILEMODE_COMMIT) ||
                        (their && their->mode == GIT_FILEMODE_COMMIT) ||
                        (anc && anc->mode == GIT_FILEMODE_COMMIT);
                    if (gitlink)
                        st.conflictedSubmodules.push_back(p);
                }
            }
        }
    }

    // Pause reason.
    if (!st.conflictedPaths.empty())
        st.pause = RebasePause::Conflict;
    else if (s.applied && cur
             && (cur->action == RebaseAction::Reword || cur->action == RebaseAction::Squash))
    {
        st.pause = RebasePause::Message;
        if (cur->action == RebaseAction::Reword)
        {
            if (auto m = commitMessage(cur->oid))
                st.messagePrefill = *m;
        }
        else // Squash: HEAD's accumulated message + this commit's message
        {
            std::string head;
            git_oid head_oid;
            if (git_reference_name_to_id(&head_oid, m_repo, "HEAD") == 0)
            {
                char hb[GIT_OID_SHA1_HEXSIZE + 1] = {0};
                git_oid_tostr(hb, sizeof(hb), &head_oid);
                if (auto hm = commitMessage(hb))
                    head = *hm;
            }
            std::string mine;
            if (auto m = commitMessage(cur->oid))
                mine = *m;
            st.messagePrefill = head + "\n\n" + mine;
        }
    }
    return st;
}

// ─────────────────────────────────────────────────────────────────────────────

RebaseState GitRepo::rebaseState() const
{
    if (interactiveRebaseInProgress())
        return interactiveRebaseState();

    RebaseState st;
    const int state = git_repository_state(m_repo);
    st.inProgress = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                 || state == GIT_REPOSITORY_STATE_REBASE
                 || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!st.inProgress)
        return st;

    st.ontoRef = rebaseOntoName();

    // Re-open the on-disk rebase to read step counts and the current commit (D30).
    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    if (git_rebase_open(&rebase, m_repo, &opts) == 0)
    {
        std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);
        st.total          = static_cast<int>(git_rebase_operation_entrycount(rebase));
        const size_t curIx = git_rebase_operation_current(rebase);
        if (curIx != GIT_REBASE_NO_OPERATION && st.total > 0)
        {
            st.current = static_cast<int>(curIx) + 1;
            if (git_rebase_operation* op = git_rebase_operation_byindex(rebase, curIx))
            {
                git_commit* c = nullptr;
                if (git_commit_lookup(&c, m_repo, &op->id) == 0)
                {
                    std::unique_ptr<git_commit, decltype(&git_commit_free)> cg(c, git_commit_free);
                    if (const char* s = git_commit_summary(c))
                        st.stepSummary = s;
                }
            }
        }
    }

    // Conflicted entries — identical derivation to mergeState().
    git_index* index = nullptr;
    if (git_repository_index(&index, m_repo) == 0)
    {
        std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
        if (git_index_has_conflicts(index))
        {
            git_index_conflict_iterator* it = nullptr;
            if (git_index_conflict_iterator_new(&it, index) == 0)
            {
                std::unique_ptr<git_index_conflict_iterator,
                                decltype(&git_index_conflict_iterator_free)>
                    itg(it, git_index_conflict_iterator_free);
                const git_index_entry *anc = nullptr, *our = nullptr, *their = nullptr;
                while (git_index_conflict_next(&anc, &our, &their, it) == 0)
                {
                    const git_index_entry* e = our ? our : (their ? their : anc);
                    if (!e || !e->path)
                        continue;
                    std::filesystem::path p = fromGitPath(e->path);
                    st.conflictedPaths.push_back(p);
                    const bool gitlink =
                        (our && our->mode == GIT_FILEMODE_COMMIT) ||
                        (their && their->mode == GIT_FILEMODE_COMMIT) ||
                        (anc && anc->mode == GIT_FILEMODE_COMMIT);
                    if (gitlink)
                        st.conflictedSubmodules.push_back(p);
                }
            }
        }
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

Expected<bool> GitRepo::stashSave(std::string message)
{
    auto st = status();
    if (!st)
        return std::unexpected(st.error());
    if (st->empty())
        return false; // clean — nothing stashed

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
    {
        if (sig)
        {
            git_signature_free(sig);
            sig = nullptr;
        }
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    }
    if (!sig)
        return std::unexpected(GitError{-1, "could not build a signature for the stash"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    git_oid stash_oid;
    int rc = git_stash_save(&stash_oid, m_repo, sig, message.c_str(), GIT_STASH_INCLUDE_UNTRACKED);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return true;
}

Expected<void> GitRepo::stashPop()
{
    git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
    int rc = git_stash_pop(m_repo, 0, &aopts);
    if (rc < 0)
        // Stash is intentionally preserved — the caller can inspect it.
        return std::unexpected(GitError{rc, "Your changes conflict and are kept in the stash"});
    return {};
}

Expected<int> GitRepo::stashCount() const
{
    int count  = 0;
    int rc     = git_stash_foreach(
        m_repo,
        [](size_t, const char*, const git_oid*, void* payload) -> int
        {
            ++*static_cast<int*>(payload);
            return 0;
        },
        &count);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return count;
}

Expected<std::vector<StashEntry>> GitRepo::stashList() const
{
    std::vector<StashEntry> entries;
    int rc = git_stash_foreach(
        m_repo,
        [](size_t index, const char* message, const git_oid* oid, void* payload) -> int
        {
            char hex[GIT_OID_HEXSZ + 1] = {0};
            git_oid_tostr(hex, sizeof(hex), oid);
            static_cast<std::vector<StashEntry>*>(payload)->push_back(
                StashEntry{index, message ? message : "", hex});
            return 0;
        },
        &entries);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return entries;
}

Expected<void> GitRepo::stashApplyAt(std::size_t index)
{
    git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
    int rc = git_stash_apply(m_repo, index, &aopts);
    if (rc < 0)
        return std::unexpected(GitError{rc, "Your changes conflict and the stash was kept"});
    return {};
}

Expected<void> GitRepo::stashPopAt(std::size_t index)
{
    git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
    int rc = git_stash_pop(m_repo, index, &aopts);
    if (rc < 0)
        // libgit2 does not drop on a conflicting pop — the stash is preserved.
        return std::unexpected(GitError{rc, "Your changes conflict and are kept in the stash"});
    return {};
}

Expected<void> GitRepo::stashDrop(std::size_t index)
{
    int rc = git_stash_drop(m_repo, index);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::stashClear()
{
    auto list = stashList();
    if (!list)
        return std::unexpected(list.error());
    // Drop highest index first so the remaining indices stay valid.
    for (auto it = list->rbegin(); it != list->rend(); ++it)
    {
        int rc = git_stash_drop(m_repo, it->index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    return {};
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

Expected<MergeOutcome> GitRepo::mergeBranch(std::string name)
{
    // Guard: never start a merge while another operation (rebase, cherry-pick,
    // etc.) is in progress — mirroring the equivalent guard in startRebase.
    if (git_repository_state(m_repo) != GIT_REPOSITORY_STATE_NONE)
        return std::unexpected(GitError{-1, "cannot merge: another operation is in progress"});
    if (interactiveRebaseInProgress())
        return std::unexpected(GitError{-1, "cannot merge: a rebase is in progress"});

    // Resolve the local branch to an annotated commit (merge analysis input).
    git_reference* ref = nullptr;
    int rc = git_branch_lookup(&ref, m_repo, name.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);

    git_annotated_commit* their = nullptr;
    rc = git_annotated_commit_from_ref(&their, m_repo, ref);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_annotated_commit, decltype(&git_annotated_commit_free)>
        their_guard(their, git_annotated_commit_free);

    const git_annotated_commit* heads[] = {their};
    git_merge_analysis_t analysis;
    git_merge_preference_t pref;
    rc = git_merge_analysis(&analysis, &pref, m_repo, heads, 1);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    MergeOutcome out;
    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE)
    {
        out.analysis = MergeAnalysis::UpToDate;
        return out;
    }

    if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)
    {
        out.analysis = MergeAnalysis::FastForward;
        const git_oid* target = git_annotated_commit_id(their);

        git_commit* tc = nullptr;
        rc = git_commit_lookup(&tc, m_repo, target);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> tc_guard(tc, git_commit_free);

        git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
        copts.checkout_strategy    = GIT_CHECKOUT_SAFE;
        rc = git_checkout_tree(m_repo, reinterpret_cast<const git_object*>(tc), &copts);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        // Move the current branch ref to the target, then point HEAD's workdir at it.
        git_reference* head_ref = nullptr;
        rc = git_repository_head(&head_ref, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head_ref, git_reference_free);
        git_reference* new_ref = nullptr;
        rc = git_reference_set_target(&new_ref, head_ref, target, "merge: fast-forward");
        if (new_ref)
            git_reference_free(new_ref);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
        git_oid_tostr(hex, sizeof(hex), target);
        out.newOid = hex;
        return out;
    }

    // Normal merge: writes into index + worktree, leaving conflicts if any.
    out.analysis = MergeAnalysis::Normal;
    git_merge_options mopts   = GIT_MERGE_OPTIONS_INIT;
    git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
    copts.checkout_strategy    = GIT_CHECKOUT_SAFE | GIT_CHECKOUT_ALLOW_CONFLICTS;
    rc = git_merge(m_repo, heads, 1, &mopts, &copts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_index* index = nullptr;
    rc = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> index_guard(index, git_index_free);
    out.conflicted = git_index_has_conflicts(index) != 0;
    return out;
}

Expected<void> GitRepo::abortMerge()
{
    git_oid head_oid;
    int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* head = nullptr;
    rc = git_commit_lookup(&head, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> head_guard(head, git_commit_free);

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy    = GIT_CHECKOUT_FORCE; // discard the half-merged worktree
    rc = git_reset(m_repo, reinterpret_cast<const git_object*>(head), GIT_RESET_HARD, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_repository_state_cleanup(m_repo);
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

// Append every delta of a tree-to-tree diff to @p out as a FileStatus, mapping
// git delta status to an index-side StatusFlag (added/deleted/modified).
static void appendDiffDeltas(git_diff* raw, std::vector<FileStatus>& out)
{
    size_t n = git_diff_num_deltas(raw);
    out.reserve(out.size() + n);
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
            out.push_back(FileStatus{fromGitPath(path), flag});
    }
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
    appendDiffDeltas(raw, result);
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

Expected<void> GitRepo::stashTrees(const std::string& oid, git_tree** outStash,
                                   git_tree** outBase, git_tree** outUntracked) const
{
    *outStash     = nullptr;
    *outBase      = nullptr;
    *outUntracked = nullptr;

    git_oid coid;
    if (int rc = git_oid_fromstr(&coid, oid.c_str()); rc < 0)
        return std::unexpected(lastGitError(rc));

    git_commit* commit = nullptr;
    if (int rc = git_commit_lookup(&commit, m_repo, &coid); rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    if (int rc = git_commit_tree(outStash, commit); rc < 0)
        return std::unexpected(lastGitError(rc));

    // Parent 0 is the base (HEAD at stash time). git stash always has it.
    const unsigned parents = git_commit_parentcount(commit);
    if (parents > 0)
    {
        git_commit* base = nullptr;
        if (git_commit_parent(&base, commit, 0) == 0)
        {
            std::unique_ptr<git_commit, decltype(&git_commit_free)> base_guard(base, git_commit_free);
            if (int rc = git_commit_tree(outBase, base); rc < 0)
            {
                git_tree_free(*outStash);
                *outStash = nullptr;
                return std::unexpected(lastGitError(rc));
            }
        }
    }

    // Parent 2 (the third) holds the untracked files, present only when the stash
    // was taken with --include-untracked.
    if (parents >= 3)
    {
        git_commit* untr = nullptr;
        if (git_commit_parent(&untr, commit, 2) == 0)
        {
            std::unique_ptr<git_commit, decltype(&git_commit_free)> untr_guard(untr, git_commit_free);
            if (int rc = git_commit_tree(outUntracked, untr); rc < 0)
            {
                git_tree_free(*outStash);
                git_tree_free(*outBase);
                *outStash = nullptr;
                *outBase  = nullptr;
                return std::unexpected(lastGitError(rc));
            }
        }
    }
    return {};
}

Expected<std::vector<FileStatus>> GitRepo::stashFiles(std::string oid) const
{
    git_tree *stashTree = nullptr, *baseTree = nullptr, *untrackedTree = nullptr;
    if (auto r = stashTrees(oid, &stashTree, &baseTree, &untrackedTree); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> s_guard(stashTree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> b_guard(baseTree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> u_guard(untrackedTree, git_tree_free);

    std::vector<FileStatus> result;

    // Tracked changes: base tree → stash tree (staged + unstaged tracked edits).
    {
        git_diff* raw = nullptr;
        if (int rc = git_diff_tree_to_tree(&raw, m_repo, baseTree, stashTree, nullptr); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_diff, decltype(&git_diff_free)> dg(raw, git_diff_free);
        appendDiffDeltas(raw, result);
    }

    // Untracked files: empty → untracked tree, so each shows as added.
    if (untrackedTree)
    {
        git_diff* raw = nullptr;
        if (int rc = git_diff_tree_to_tree(&raw, m_repo, nullptr, untrackedTree, nullptr); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_diff, decltype(&git_diff_free)> dg(raw, git_diff_free);
        appendDiffDeltas(raw, result);
    }
    return result;
}

Expected<DiffResult> GitRepo::stashDiff(std::string oid, const std::filesystem::path& file) const
{
    git_tree *stashTree = nullptr, *baseTree = nullptr, *untrackedTree = nullptr;
    if (auto r = stashTrees(oid, &stashTree, &baseTree, &untrackedTree); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> s_guard(stashTree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> b_guard(baseTree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> u_guard(untrackedTree, git_tree_free);

    std::string git_file  = toGitPath(file);
    char* paths[]         = {git_file.data()};
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count   = 1;

    // Tracked file first: base → stash for this path.
    {
        git_diff* raw = nullptr;
        if (int rc = git_diff_tree_to_tree(&raw, m_repo, baseTree, stashTree, &opts); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_diff, decltype(&git_diff_free)> dg(raw, git_diff_free);
        if (git_diff_num_deltas(raw) > 0)
            return DiffEngine::parse(raw);
    }

    // Otherwise it may be an untracked file: empty → untracked tree for this path.
    if (untrackedTree)
    {
        git_diff* raw = nullptr;
        if (int rc = git_diff_tree_to_tree(&raw, m_repo, nullptr, untrackedTree, &opts); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_diff, decltype(&git_diff_free)> dg(raw, git_diff_free);
        if (git_diff_num_deltas(raw) > 0)
            return DiffEngine::parse(raw);
    }
    return DiffResult{}; // path in neither tracked nor untracked set
}

Expected<std::vector<FileStatus>> GitRepo::rangeFiles(std::string oldOid, std::string newOid) const
{
    // Older endpoint's first-parent tree (null for a root commit).
    git_tree* oldOwn    = nullptr;
    git_tree* oldParent = nullptr;
    if (auto r = commitTrees(oldOid, &oldOwn, &oldParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldOwn_guard(oldOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldParent_guard(oldParent, git_tree_free);

    // Newer endpoint's own tree.
    git_tree* newOwn    = nullptr;
    git_tree* newParent = nullptr;
    if (auto r = commitTrees(newOid, &newOwn, &newParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newOwn_guard(newOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newParent_guard(newParent, git_tree_free);

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, oldParent, newOwn, nullptr);
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
            default:
                flag = StatusFlag::IndexModified;
                break;
        }
        if (path)
            result.push_back(FileStatus{fromGitPath(path), flag});
    }
    return result;
}

Expected<DiffResult> GitRepo::rangeDiff(std::string oldOid, std::string newOid,
                                        const std::filesystem::path& file) const
{
    git_tree* oldOwn    = nullptr;
    git_tree* oldParent = nullptr;
    if (auto r = commitTrees(oldOid, &oldOwn, &oldParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldOwn_guard(oldOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldParent_guard(oldParent, git_tree_free);

    git_tree* newOwn    = nullptr;
    git_tree* newParent = nullptr;
    if (auto r = commitTrees(newOid, &newOwn, &newParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newOwn_guard(newOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newParent_guard(newParent, git_tree_free);

    std::string git_file  = toGitPath(file);
    char* paths[]         = {git_file.data()};
    git_diff_options opts  = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count   = 1;

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, oldParent, newOwn, &opts);
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

Expected<std::pair<int, int>> GitRepo::aheadBehind(std::string localOid, std::string baseOid) const
{
    git_oid local{};
    git_oid base{};
    if (int rc = git_oid_fromstr(&local, localOid.c_str()); rc < 0)
        return std::unexpected(lastGitError(rc));
    if (int rc = git_oid_fromstr(&base, baseOid.c_str()); rc < 0)
        return std::unexpected(lastGitError(rc));

    std::size_t ahead = 0;
    std::size_t behind = 0;
    if (int rc = git_graph_ahead_behind(&ahead, &behind, m_repo, &local, &base); rc < 0)
        return std::unexpected(lastGitError(rc));

    return std::pair<int, int>{static_cast<int>(ahead), static_cast<int>(behind)};
}

Expected<std::vector<std::string>> GitRepo::localOnlyOids() const
{
    git_revwalk* walk = nullptr;
    int          rc   = git_revwalk_new(&walk, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    rc = git_revwalk_push_head(walk);
    if (rc < 0)
    {
        git_revwalk_free(walk);
        // Unborn/missing HEAD => nothing to walk, so nothing is local-only.
        if (git_repository_head_unborn(m_repo) > 0)
            return std::vector<std::string>{};
        return std::unexpected(lastGitError(rc));
    }

    // Hiding every remote-tracking tip leaves exactly the commits reachable from
    // HEAD but from no remote — the not-yet-pushed set. A missing glob (no remotes)
    // is not fatal: then nothing is hidden and every HEAD commit is local-only.
    git_revwalk_hide_glob(walk, "refs/remotes/*");

    std::vector<std::string> result;
    git_oid                  oid;
    while (git_revwalk_next(&oid, walk) == 0)
    {
        char hex[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(hex, sizeof(hex), &oid);
        result.emplace_back(hex);
    }

    git_revwalk_free(walk);
    return result;
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

Expected<void> GitRepo::setLocalIdentity(std::string name, std::string email, std::string marker)
{
    git_config* cfg = nullptr;
    int rc          = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    // git_config_set_* on the repo config writes to the highest-priority writable
    // level — the repo-local .git/config — exactly like setPullStrategy above.
    rc = git_config_set_string(cfg, "user.name", name.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    rc = git_config_set_string(cfg, "user.email", email.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    rc = git_config_set_string(cfg, "gittide.identity", marker.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::clearLocalIdentity()
{
    git_config* cfg = nullptr;
    int rc          = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    // Delete only from the LOCAL level: deleting via the merged config could
    // remove a global entry if the repo has no local one. A missing local level
    // (or missing key) means there is nothing to clear — not an error.
    git_config* local = nullptr;
    rc                = git_config_open_level(&local, cfg, GIT_CONFIG_LEVEL_LOCAL);
    if (rc == GIT_ENOTFOUND)
        return {};
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> localGuard(local, git_config_free);

    for (const char* key : {"user.name", "user.email", "gittide.identity"})
    {
        rc = git_config_delete_entry(local, key);
        if (rc < 0 && rc != GIT_ENOTFOUND)
            return std::unexpected(lastGitError(rc));
    }
    return {};
}

Expected<void> GitRepo::setGlobalIdentity(std::string name, std::string email)
{
    // Locate ~/.gitconfig; if none exists yet, derive its path from the global
    // config search path so the first write creates the file.
    std::string path;
    git_buf     found = GIT_BUF_INIT;
    int         rc    = git_config_find_global(&found);
    if (rc == 0)
        path.assign(found.ptr, found.size);
    git_buf_dispose(&found);

    if (path.empty())
    {
        git_buf sp = GIT_BUF_INIT;
        if (git_libgit2_opts(GIT_OPT_GET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, &sp) == 0 && sp.size > 0)
        {
            std::string dir(sp.ptr, sp.size);
#ifdef _WIN32
            const char listSep = ';';
#else
            const char listSep = ':';
#endif
            if (auto pos = dir.find(listSep); pos != std::string::npos)
                dir.resize(pos); // first search-path entry only
            if (!dir.empty())
                path = dir + "/.gitconfig";
        }
        git_buf_dispose(&sp);
    }
    if (path.empty())
        return std::unexpected(GitError{-1, "cannot locate global git config path"});

    git_config* raw = nullptr;
    rc              = git_config_open_ondisk(&raw, path.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(raw, git_config_free);

    rc = git_config_set_string(raw, "user.name", name.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    rc = git_config_set_string(raw, "user.email", email.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<ConfigIdentity> GitRepo::effectiveIdentity() const
{
    git_config* cfg = nullptr;
    int         rc  = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    ConfigIdentity id;
    auto           readKey = [&](const char* key, std::string& out)
    {
        git_buf buf = GIT_BUF_INIT;
        if (git_config_get_string_buf(&buf, cfg, key) == 0)
            out.assign(buf.ptr, buf.size);
        git_buf_dispose(&buf);
    };
    readKey("user.name", id.name);
    readKey("user.email", id.email);
    return id;
}

Expected<ConfigIdentity> GitRepo::globalIdentity()
{
    git_config* cfg = nullptr;
    int         rc  = git_config_open_default(&cfg);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    ConfigIdentity id;
    auto           readKey = [&](const char* key, std::string& out)
    {
        git_buf buf = GIT_BUF_INIT;
        if (git_config_get_string_buf(&buf, cfg, key) == 0)
            out.assign(buf.ptr, buf.size);
        git_buf_dispose(&buf);
    };
    readKey("user.name", id.name);
    readKey("user.email", id.email);
    return id;
}

Expected<LocalIdentityInfo> GitRepo::localIdentity() const
{
    git_config* cfg = nullptr;
    int         rc  = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    LocalIdentityInfo info;
    git_config*       local = nullptr;
    rc                      = git_config_open_level(&local, cfg, GIT_CONFIG_LEVEL_LOCAL);
    if (rc == GIT_ENOTFOUND)
        return info; // no local level → all-empty
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> localGuard(local, git_config_free);

    auto readKey = [&](const char* key, std::string& out) -> bool
    {
        git_buf buf = GIT_BUF_INIT;
        bool    ok  = git_config_get_string_buf(&buf, local, key) == 0;
        if (ok)
            out.assign(buf.ptr, buf.size);
        git_buf_dispose(&buf);
        return ok;
    };
    info.hasName  = readKey("user.name", info.name);
    info.hasEmail = readKey("user.email", info.email);
    info.managed  = readKey("gittide.identity", info.marker);
    return info;
}

Expected<std::string> GitRepo::remoteUrl(std::string remoteName) const
{
    git_remote* raw = nullptr;
    int         rc  = git_remote_lookup(&raw, m_repo, remoteName.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_remote, decltype(&git_remote_free)> remote(raw, git_remote_free);
    const char* url = git_remote_url(remote.get());
    return std::string(url ? url : "");
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
    git_push_options opts                = GIT_PUSH_OPTIONS_INIT;
    opts.callbacks.credentials           = credentialTrampoline;
    opts.callbacks.push_transfer_progress = pushTransferProgressTrampoline;
    opts.callbacks.payload               = &pl;

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

Expected<RebaseOutcome> GitRepo::driveRebase(git_rebase* rebase, git_signature* sig)
{
    RebaseOutcome out;
    while (true)
    {
        git_rebase_operation* op = nullptr;
        int rc = git_rebase_next(&op, rebase);
        if (rc == GIT_ITEROVER)
        {
            rc = git_rebase_finish(rebase, sig);
            if (rc < 0)
                return std::unexpected(lastGitError(rc));
            out.conflicted = false;
            return out;
        }
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        // Did applying this operation leave conflicts? Pause if so (state on disk).
        git_index* index = nullptr;
        rc = git_repository_index(&index, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
        if (git_index_has_conflicts(index))
        {
            out.conflicted = true;
            return out;
        }

        // Commit the applied step, reusing its original author/message.
        git_oid id;
        rc = git_rebase_commit(&id, rebase, nullptr, sig, nullptr, nullptr);
        if (rc == GIT_EAPPLIED)
            continue; // change already upstream → implicit skip
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
}

Expected<RebaseOutcome> GitRepo::continueRebase(std::optional<std::string> message)
{
    // Interactive rebase in progress — delegate to the manual driver.
    if (interactiveRebaseInProgress())
        return driveInteractive(std::move(message));

    const int state = git_repository_state(m_repo);
    const bool rebasing = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                       || state == GIT_REPOSITORY_STATE_REBASE
                       || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!rebasing)
        return std::unexpected(GitError{-1, "no rebase in progress"});

    git_index* index = nullptr;
    int rc = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    {
        std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
        if (git_index_has_conflicts(index))
            return std::unexpected(GitError{-1, "cannot continue: unresolved conflicts remain"});
    }

    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    rc = git_rebase_open(&rebase, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    // Commit the just-resolved current operation, then advance.
    git_oid id;
    rc = git_rebase_commit(&id, rebase, nullptr, sig, nullptr, nullptr);
    if (rc < 0 && rc != GIT_EAPPLIED)
        return std::unexpected(lastGitError(rc));

    return driveRebase(rebase, sig);
}

Expected<RebaseOutcome> GitRepo::skipRebase()
{
    if (interactiveRebaseInProgress())
    {
        auto loaded = loadInteractiveState();
        if (!loaded)
            return std::unexpected(loaded.error());
        InteractiveState st = *loaded;
        // Discard the half-applied cherry-pick: hard-reset to detached HEAD.
        git_oid head_oid;
        if (int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD"); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_commit* head = nullptr;
        if (int rc = git_commit_lookup(&head, m_repo, &head_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> hg(head, git_commit_free);
        if (int rc = git_reset(m_repo, reinterpret_cast<const git_object*>(head),
                               GIT_RESET_HARD, nullptr); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_repository_state_cleanup(m_repo);
        // Advance past the current entry without committing it.
        st.applied = false;
        ++st.done;
        if (auto r = setInteractiveProgress(st.done, st.applied); !r)
            return std::unexpected(r.error());
        return driveInteractive(std::nullopt);
    }

    const int state = git_repository_state(m_repo);
    const bool rebasing = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                       || state == GIT_REPOSITORY_STATE_REBASE
                       || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!rebasing)
        return std::unexpected(GitError{-1, "no rebase in progress"});

    // Open the on-disk rebase BEFORE touching the worktree; git_reset --hard
    // clears the rebase-merge directory and invalidates any subsequent open.
    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    int rc = git_rebase_open(&rebase, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    // Discard the conflicted/half-applied worktree so the next patch applies cleanly.
    // Use checkout (not reset) so the rebase-merge state files are left intact.
    git_oid head_oid;
    rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* head_commit = nullptr;
    rc = git_commit_lookup(&head_commit, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    {
        std::unique_ptr<git_commit, decltype(&git_commit_free)> hg(head_commit, git_commit_free);
        git_tree* head_tree = nullptr;
        rc = git_commit_tree(&head_tree, head_commit);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_tree, decltype(&git_tree_free)> tg(head_tree, git_tree_free);
        git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
        copts.checkout_strategy    = GIT_CHECKOUT_FORCE;
        rc = git_checkout_tree(m_repo, reinterpret_cast<const git_object*>(head_tree), &copts);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        // Also reset the index to HEAD so conflict entries are gone.
        rc = git_reset(m_repo, reinterpret_cast<const git_object*>(head_commit), GIT_RESET_MIXED, nullptr);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    // driveRebase starts with git_rebase_next, abandoning the current op's commit.
    return driveRebase(rebase, sig);
}

Expected<void> GitRepo::abortRebase()
{
    if (interactiveRebaseInProgress())
    {
        auto loaded = loadInteractiveState();
        if (!loaded)
        {
            clearInteractiveState();
            return std::unexpected(GitError{-1, "no rebase in progress"});
        }
        const std::string refname = "refs/heads/" + loaded->branch;
        // The branch ref was never moved during the drive → it still points at
        // orig-head. Reattach HEAD and hard-reset the worktree to it.
        if (int rc = git_repository_set_head(m_repo, refname.c_str()); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_oid orig_oid;
        if (git_oid_fromstr(&orig_oid, loaded->origHead.c_str()) < 0)
            return std::unexpected(GitError{-1, "bad orig-head"});
        git_object* obj = nullptr;
        if (int rc = git_object_lookup(&obj, m_repo, &orig_oid, GIT_OBJECT_COMMIT); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_object, decltype(&git_object_free)> og(obj, git_object_free);
        if (int rc = git_reset(m_repo, obj, GIT_RESET_HARD, nullptr); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_repository_state_cleanup(m_repo);
        clearInteractiveState();
        return {};
    }

    const int state = git_repository_state(m_repo);
    const bool rebasing = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                       || state == GIT_REPOSITORY_STATE_REBASE
                       || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!rebasing)
        return std::unexpected(GitError{-1, "no rebase in progress"});

    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    int rc = git_rebase_open(&rebase, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    rc = git_rebase_abort(rebase);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<RebaseOutcome> GitRepo::startRebase(std::string ontoRef)
{
    // Guard: never start over a merge or an existing rebase.
    const int state = git_repository_state(m_repo);
    if (state != GIT_REPOSITORY_STATE_NONE)
        return std::unexpected(GitError{-1, "cannot rebase: another operation is in progress"});
    if (interactiveRebaseInProgress())
        return std::unexpected(GitError{-1, "cannot rebase: a rebase is already in progress"});

    // Guard: need a born, attached HEAD (a branch to move).
    if (git_repository_head_unborn(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is unborn"});
    if (git_repository_head_detached(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is detached"});

    // Resolve the target branch to an annotated commit (the upstream).
    git_reference* ref = nullptr;
    int rc = git_branch_lookup(&ref, m_repo, ontoRef.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);

    git_annotated_commit* upstream = nullptr;
    rc = git_annotated_commit_from_ref(&upstream, m_repo, ref);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_annotated_commit, decltype(&git_annotated_commit_free)>
        up_guard(upstream, git_annotated_commit_free);

    // branch == NULL → use HEAD; onto == NULL → defaults to upstream. This is the
    // exact "git rebase <upstream>" semantics: replay upstream..HEAD onto upstream.
    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    rc = git_rebase_init(&rebase, m_repo, nullptr, upstream, nullptr, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    return driveRebase(rebase, sig);
}

Expected<RebaseOutcome> GitRepo::startInteractiveRebase(RebaseTodo todo)
{
    // Guards (D33 mutual exclusion + structural).
    if (interactiveRebaseInProgress())
        return std::unexpected(GitError{-1, "cannot rebase: a rebase is already in progress"});
    if (git_repository_state(m_repo) != GIT_REPOSITORY_STATE_NONE)
        return std::unexpected(GitError{-1, "cannot rebase: another operation is in progress"});
    if (git_repository_head_unborn(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is unborn"});
    if (git_repository_head_detached(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is detached"});
    if (todo.entries.empty())
        return std::unexpected(GitError{-1, "cannot rebase: empty plan"});
    // The first *kept* entry cannot squash/fixup: there is no prior in-range
    // commit to fold into (folding into `base` would rewrite a commit outside the
    // range). Leading drops do not change this — find the first non-drop entry.
    const RebaseTodoEntry* firstKept = nullptr;
    for (const auto& e : todo.entries)
        if (e.action != RebaseAction::Drop) { firstKept = &e; break; }
    if (!firstKept)
        return std::unexpected(GitError{-1, "cannot rebase: all entries dropped"});
    if (firstKept->action == RebaseAction::Squash
        || firstKept->action == RebaseAction::Fixup)
        return std::unexpected(GitError{-1, "cannot rebase: first kept entry cannot be squash/fixup"});

    // Current branch + tip.
    git_reference* head_ref = nullptr;
    if (git_repository_head(&head_ref, m_repo) < 0)
        return std::unexpected(GitError{-1, "cannot resolve HEAD"});
    std::unique_ptr<git_reference, decltype(&git_reference_free)> hr(head_ref, git_reference_free);
    const char* branch_name = nullptr;
    if (git_branch_name(&branch_name, head_ref) < 0 || !branch_name)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is not a branch"});
    const std::string branch = branch_name;

    git_oid orig_oid;
    if (git_reference_name_to_id(&orig_oid, m_repo, "HEAD") < 0)
        return std::unexpected(GitError{-1, "cannot resolve HEAD"});
    char origbuf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(origbuf, sizeof(origbuf), &orig_oid);
    const std::string origHead = origbuf;

    // base must be an ancestor of HEAD.
    git_oid base_oid;
    if (git_oid_fromstr(&base_oid, todo.base.c_str()) < 0)
        return std::unexpected(GitError{-1, "cannot rebase: bad base oid"});
    if (git_graph_descendant_of(m_repo, &orig_oid, &base_oid) != 1)
        return std::unexpected(GitError{-1, "cannot rebase: base is not an ancestor of HEAD"});

    // Detach HEAD onto base.
    git_commit* base_commit = nullptr;
    if (git_commit_lookup(&base_commit, m_repo, &base_oid) < 0)
        return std::unexpected(GitError{-1, "cannot rebase: base is not a commit"});
    std::unique_ptr<git_commit, decltype(&git_commit_free)> bg(base_commit, git_commit_free);
    {
        git_checkout_options co = GIT_CHECKOUT_OPTIONS_INIT;
        co.checkout_strategy    = GIT_CHECKOUT_FORCE;
        if (int rc = git_checkout_tree(m_repo, reinterpret_cast<const git_object*>(base_commit), &co); rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    if (int rc = git_repository_set_head_detached(m_repo, &base_oid); rc < 0)
        return std::unexpected(lastGitError(rc));

    if (auto r = initInteractiveState(todo, branch, origHead); !r)
        return std::unexpected(r.error());
    return driveInteractive(std::nullopt);
}

Expected<RebaseOutcome> GitRepo::driveInteractive(std::optional<std::string> message)
{
    auto loaded = loadInteractiveState();
    if (!loaded)
        return std::unexpected(loaded.error());
    InteractiveState st = *loaded;

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    while (st.done < static_cast<int>(st.todo.entries.size()))
    {
        const RebaseTodoEntry& e = st.todo.entries[st.done];

        if (e.action == RebaseAction::Drop)
        {
            st.applied = false;
            ++st.done;
            if (auto r = setInteractiveProgress(st.done, st.applied); !r)
                return std::unexpected(r.error());
            message.reset();
            continue;
        }

        // 1) Apply the commit if not already applied.
        if (!st.applied)
        {
            git_oid pick_oid;
            if (git_oid_fromstr(&pick_oid, e.oid.c_str()) < 0)
                return std::unexpected(GitError{-1, "bad oid in todo"});
            git_commit* pick = nullptr;
            if (int rc = git_commit_lookup(&pick, m_repo, &pick_oid); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_commit, decltype(&git_commit_free)> pg(pick, git_commit_free);

            git_cherrypick_options copts = GIT_CHERRYPICK_OPTIONS_INIT;
            copts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
            if (int rc = git_cherrypick(m_repo, pick, &copts); rc < 0)
                return std::unexpected(lastGitError(rc));

            st.applied = true;
            if (auto r = setInteractiveProgress(st.done, st.applied); !r)
                return std::unexpected(r.error());

            git_index* index = nullptr;
            if (int rc = git_repository_index(&index, m_repo); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
            if (git_index_has_conflicts(index))
                return RebaseOutcome{true, RebasePause::Conflict};
        }
        else
        {
            // Re-entry after a conflict was resolved + staged.
            git_index* index = nullptr;
            if (int rc = git_repository_index(&index, m_repo); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
            if (git_index_has_conflicts(index))
                return std::unexpected(GitError{-1, "cannot continue: unresolved conflicts remain"});
        }

        // 2) reword/squash need a message before we can commit.
        if ((e.action == RebaseAction::Reword || e.action == RebaseAction::Squash)
            && !message.has_value())
            return RebaseOutcome{false, RebasePause::Message};

        // Build the tree from the resolved index.
        git_oid tree_oid;
        {
            git_index* index = nullptr;
            if (int rc = git_repository_index(&index, m_repo); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
            if (int rc = git_index_write_tree(&tree_oid, index); rc < 0)
                return std::unexpected(lastGitError(rc));
        }
        git_tree* tree = nullptr;
        if (int rc = git_tree_lookup(&tree, m_repo, &tree_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_tree, decltype(&git_tree_free)> tg(tree, git_tree_free);

        // current detached HEAD commit = parent (pick/reword) or amend target (squash/fixup)
        git_oid head_oid;
        if (int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD"); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_commit* head = nullptr;
        if (int rc = git_commit_lookup(&head, m_repo, &head_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> hg(head, git_commit_free);

        // original commit for author + message
        git_oid orig_oid;
        git_oid_fromstr(&orig_oid, e.oid.c_str());
        git_commit* orig = nullptr;
        if (int rc = git_commit_lookup(&orig, m_repo, &orig_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> og(orig, git_commit_free);

        git_oid newc;
        int rc = 0;
        if (e.action == RebaseAction::Pick)
        {
            git_commit* parents[1] = {head};
            rc = git_commit_create(&newc, m_repo, "HEAD", git_commit_author(orig), sig,
                                   nullptr, git_commit_message(orig), tree, 1, parents);
        }
        else if (e.action == RebaseAction::Reword)
        {
            git_commit* parents[1] = {head};
            rc = git_commit_create(&newc, m_repo, "HEAD", git_commit_author(orig), sig,
                                   nullptr, message->c_str(), tree, 1, parents);
        }
        else if (e.action == RebaseAction::Fixup)
        {
            rc = git_commit_amend(&newc, head, "HEAD", git_commit_author(head), sig,
                                  nullptr, git_commit_message(head), tree);
        }
        else // Squash
        {
            rc = git_commit_amend(&newc, head, "HEAD", git_commit_author(head), sig,
                                  nullptr, message->c_str(), tree);
        }
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        git_repository_state_cleanup(m_repo); // clear CHERRY_PICK_HEAD/MERGE_MSG
        st.applied = false;
        ++st.done;
        if (auto r = setInteractiveProgress(st.done, st.applied); !r)
            return std::unexpected(r.error());
        message.reset(); // a message applies only to the step that paused for it
    }

    return finishInteractive(st);
}

Expected<RebaseOutcome> GitRepo::finishInteractive(const InteractiveState& st)
{
    git_oid head_oid;
    if (int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD"); rc < 0)
        return std::unexpected(lastGitError(rc));

    const std::string refname = "refs/heads/" + st.branch;
    git_reference* newref = nullptr;
    if (int rc = git_reference_create(&newref, m_repo, refname.c_str(), &head_oid,
                                      /*force=*/1, "gittide: interactive rebase"); rc < 0)
        return std::unexpected(lastGitError(rc));
    git_reference_free(newref);

    if (int rc = git_repository_set_head(m_repo, refname.c_str()); rc < 0)
    {
        clearInteractiveState();
        return std::unexpected(lastGitError(rc));
    }

    clearInteractiveState();
    git_repository_state_cleanup(m_repo);
    return RebaseOutcome{false, RebasePause::None};
}

} // namespace gittide
