#include <catch2/catch_test_macros.hpp>
#include "gittide/diffengine.hpp"
#include "gittide/pathutil.hpp"
#include "support/TempRepo.hpp"
#include <git2.h>
#include <memory>

using gittide::DiffEngine;
using gittide::DiffLineOrigin;

// Helper: worktree-vs-index diff for a single file, parsed via DiffEngine.
static gittide::DiffResult diff_file(const std::filesystem::path& repo_path,
                                    const char* file) {
    git_repository* repo = nullptr;
    REQUIRE(git_repository_open(&repo, gittide::to_git_path(repo_path).c_str()) == 0);

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    char* paths[] = {const_cast<char*>(file)};
    opts.pathspec.strings = paths;
    opts.pathspec.count = 1;

    git_diff* diff = nullptr;
    REQUIRE(git_diff_index_to_workdir(&diff, repo, nullptr, &opts) == 0);
    auto result = DiffEngine::parse(diff);
    git_diff_free(diff);
    git_repository_free(repo);
    REQUIRE(result.has_value());
    return *result;
}

TEST_CASE("DiffEngine parses a single modified hunk", "[diff]") {
    gittide::test::TempRepo tmp;
    tmp.write_file("a.txt", "line1\nline2\nline3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "line1\nCHANGED\nline3\n");

    auto d = diff_file(tmp.path(), "a.txt");
    REQUIRE(d.hunks.size() == 1);
    const auto& h = d.hunks[0];

    // Expect: context line1, -line2, +CHANGED, context line3.
    int added = 0, removed = 0, context = 0;
    for (const auto& ln : h.lines) {
        if (ln.origin == DiffLineOrigin::Added) ++added;
        else if (ln.origin == DiffLineOrigin::Removed) ++removed;
        else ++context;
    }
    REQUIRE(added == 1);
    REQUIRE(removed == 1);
    REQUIRE(context == 2);
}
