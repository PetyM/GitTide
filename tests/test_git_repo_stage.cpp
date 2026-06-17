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

TEST_CASE("stage whole file with no trailing newline does not corrupt", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "first\nsecond");   // NO trailing newline
    tmp.commit_all("init");
    tmp.write_file("a.txt", "first\nCHANGED");  // still no trailing newline

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    // Staged content matches the worktree exactly (incl. absence of trailing nl).
    auto unstaged = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.empty());   // nothing left unstaged
}

TEST_CASE("stage a no-trailing-newline change via hunk patch", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "alpha\nbeta");      // no trailing newline
    tmp.commit_all("init");
    tmp.write_file("a.txt", "alpha\nBETA");      // change last line, still no nl

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    // Stage that hunk; the patch must apply cleanly despite the missing newline.
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", 0, {}}).has_value());

    auto staged = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);
    auto unstaged = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.empty());
}

TEST_CASE("stage a single line of a multi-line addition", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "a\nb\nc\n");
    tmp.commit_all("init");
    // Insert two lines (X, Y) after 'a'. Hunk: ctx a, +X, +Y, ctx b, ctx c.
    tmp.write_file("a.txt", "a\nX\nY\nb\nc\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);
    const auto& hunk = d->hunks[0];

    // Find the line index of the added "X".
    int xIdx = -1;
    for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i) {
        if (hunk.lines[i].origin == gitgui::DiffLineOrigin::Added &&
            hunk.lines[i].text == "X") { xIdx = i; break; }
    }
    REQUIRE(xIdx >= 0);

    // Stage ONLY the "X" line.
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", 0, {xIdx}}).has_value());

    // Index vs HEAD: exactly one added line, and it is "X" (not Y).
    auto staged = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);
    int addedCount = 0; bool sawX = false, sawY = false;
    for (const auto& ln : staged->hunks[0].lines) {
        if (ln.origin == gitgui::DiffLineOrigin::Added) {
            ++addedCount;
            if (ln.text == "X") sawX = true;
            if (ln.text == "Y") sawY = true;
        }
    }
    REQUIRE(addedCount == 1);
    REQUIRE(sawX);
    REQUIRE_FALSE(sawY);

    // Y is still pending in the worktree.
    auto unstaged = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.size() == 1);
}
