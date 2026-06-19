#include "gittide/gitrepo.hpp"

#include <git2.h>
#include <git2/branch.h>
#include <git2/checkout.h>
#include <git2/commit.h>
#include <git2/graph.h>
#include <git2/reset.h>
#include <git2/revwalk.h>
#include <git2/stash.h>
#include <memory>
#include <utility>
#include <vector>

#include "gittide/diffengine.hpp"
#include "gittide/pathutil.hpp"

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

int transferProgressTrampoline(const git_indexer_progress* stats, void* payload)
{
    auto* cb = static_cast<gittide::ProgressCallback*>(payload);
    return (*cb)(stats->received_objects, stats->total_objects) ? 0 : -1;
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
        return std::unexpected(lastGitError(rc));
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
    git_clone_options opts                      = GIT_CLONE_OPTIONS_INIT;
    opts.fetch_opts.callbacks.transfer_progress = transferProgressTrampoline;
    opts.fetch_opts.callbacks.payload           = &cb; // cb lives on the stack for the duration

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

Expected<std::vector<std::filesystem::path>> GitRepo::submodules() const
{
    std::vector<std::filesystem::path> result;
    const std::filesystem::path wd = workdir();

    struct Payload
    {
        std::vector<std::filesystem::path>* out;
        const std::filesystem::path* wd;
    };
    Payload payload{&result, &wd};

    auto cb = [](git_submodule* sm, const char* /*name*/, void* pl) -> int
    {
        auto* p         = static_cast<Payload*>(pl);
        const char* rel = git_submodule_path(sm);
        if (rel)
            p->out->push_back(*p->wd / fromGitPath(rel));
        return 0;
    };

    const int rc = git_submodule_foreach(m_repo, cb, &payload);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return result;
}

Expected<std::vector<BranchInfo>> GitRepo::branches() const
{
    std::vector<BranchInfo> result;

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

} // namespace gittide
