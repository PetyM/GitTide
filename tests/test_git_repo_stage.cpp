#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"
#include <algorithm>

using gitgui::StatusFlag;
using gitgui::has_flag;

static StatusFlag flags_for(const gitgui::GitRepo& repo, const char* file) {
    auto st = repo.status();
    REQUIRE(st.has_value());
    auto it = std::find_if(st->begin(), st->end(), [&](const gitgui::FileStatus& f) {
        return f.path == std::filesystem::path(file);
    });
    return it == st->end() ? StatusFlag::None : it->flags;
}

TEST_CASE("stage whole file moves WtModified to IndexModified", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "1\n2\n3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "1\nTWO\n3\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::WtModified));

    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::IndexModified));
}

TEST_CASE("unstage whole file moves IndexModified back to WtModified", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "1\n2\n3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "1\nTWO\n3\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::IndexModified));

    REQUIRE(repo->unstage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::WtModified));
}

TEST_CASE("stage whole file handles deletion", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("gone.txt", "bye\n");
    tmp.commit_all("init");
    std::filesystem::remove(tmp.path() / "gone.txt");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(has_flag(flags_for(*repo, "gone.txt"), StatusFlag::WtDeleted));

    REQUIRE(repo->stage(gitgui::StageSelection{"gone.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "gone.txt"), StatusFlag::IndexDeleted));
}

TEST_CASE("stage a single hunk stages only that change", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "test@example.com");
    // Two separate change regions far apart so they form two hunks.
    tmp.write_file("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "ONE\n2\n3\n4\n5\n6\n7\n8\nNINE\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 2);

    // Stage only the first hunk.
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", 0, {}}).has_value());

    // Staged diff (index vs HEAD) now contains exactly one hunk (the first).
    auto staged = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);

    // The worktree still has the second change unstaged.
    auto unstaged = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.size() == 1);
}

TEST_CASE("unstage a staged hunk returns it to the worktree", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "test@example.com");
    tmp.write_file("a.txt", "1\n2\n3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "1\nTWO\n3\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto staged = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);

    // Unstage that one hunk.
    REQUIRE(repo->unstage(gitgui::StageSelection{"a.txt", 0, {}}).has_value());

    auto after = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(after.has_value());
    REQUIRE(after->hunks.empty());
}
