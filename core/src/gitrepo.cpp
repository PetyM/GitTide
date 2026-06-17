#include "gittide/gitrepo.hpp"

#include <git2.h>
#include <git2/commit.h>
#include <git2/revwalk.h>
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
    else
    {
        // IndexVsHead: compare HEAD's tree to the index. Unborn HEAD -> null tree.
        git_object* head_obj = nullptr;
        git_tree* head_tree  = nullptr;
        if (git_revparse_single(&head_obj, m_repo, "HEAD^{tree}") == 0)
        {
            head_tree = reinterpret_cast<git_tree*>(head_obj);
        }
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

} // namespace gittide
