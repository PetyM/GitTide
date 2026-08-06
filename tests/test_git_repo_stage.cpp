#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::hasFlag;
using gittide::StatusFlag;

static StatusFlag flags_for(const gittide::GitRepo& repo, const char* file)
{
    auto st = repo.status();
    REQUIRE(st.has_value());
    auto it = std::find_if(st->begin(),
                           st->end(),
                           [&](const gittide::FileStatus& f)
                           {
                               return f.path == std::filesystem::path(file);
                           });
    return it == st->end() ? StatusFlag::None : it->flags;
}

TEST_CASE("stage whole file moves WtModified to IndexModified", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "1\n2\n3\n");
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "1\nTWO\n3\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(hasFlag(flags_for(*repo, "a.txt"), StatusFlag::WtModified));

    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(hasFlag(flags_for(*repo, "a.txt"), StatusFlag::IndexModified));
}

TEST_CASE("unstage whole file moves IndexModified back to WtModified", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "1\n2\n3\n");
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "1\nTWO\n3\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(hasFlag(flags_for(*repo, "a.txt"), StatusFlag::IndexModified));

    REQUIRE(repo->unstage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(hasFlag(flags_for(*repo, "a.txt"), StatusFlag::WtModified));
}

TEST_CASE("stage whole file handles deletion", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("gone.txt", "bye\n");
    tmp.commitAll("init");
    std::filesystem::remove(tmp.path() / "gone.txt");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(hasFlag(flags_for(*repo, "gone.txt"), StatusFlag::WtDeleted));

    REQUIRE(repo->stage(gittide::StageSelection{"gone.txt", std::nullopt, {}}).has_value());
    REQUIRE(hasFlag(flags_for(*repo, "gone.txt"), StatusFlag::IndexDeleted));
}

TEST_CASE("stage a single hunk stages only that change", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    // Two separate change regions far apart so they form two hunks.
    tmp.writeFile("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "ONE\n2\n3\n4\n5\n6\n7\n8\nNINE\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 2);

    // Stage only the first hunk.
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", 0, {}}).has_value());

    // Staged diff (index vs HEAD) now contains exactly one hunk (the first).
    auto staged = repo->diff(gittide::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);

    // The worktree still has the second change unstaged.
    auto unstaged = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.size() == 1);
}

TEST_CASE("unstage a staged hunk returns it to the worktree", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "1\n2\n3\n");
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "1\nTWO\n3\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto staged = repo->diff(gittide::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);

    // Unstage that one hunk.
    REQUIRE(repo->unstage(gittide::StageSelection{"a.txt", 0, {}}).has_value());

    auto after = repo->diff(gittide::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(after.has_value());
    REQUIRE(after->hunks.empty());
}

TEST_CASE("stage whole file with no trailing newline does not corrupt", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "first\nsecond"); // NO trailing newline
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "first\nCHANGED"); // still no trailing newline

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    // Staged content matches the worktree exactly (incl. absence of trailing nl).
    auto unstaged = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.empty()); // nothing left unstaged
}

TEST_CASE("stage a no-trailing-newline change via hunk patch", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "alpha\nbeta"); // no trailing newline
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "alpha\nBETA"); // change last line, still no nl

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    // Stage that hunk; the patch must apply cleanly despite the missing newline.
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", 0, {}}).has_value());

    auto staged = repo->diff(gittide::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);
    auto unstaged = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.empty());
}

TEST_CASE("stage a single line of a multi-line addition", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "a\nb\nc\n");
    tmp.commitAll("init");
    // Insert two lines (X, Y) after 'a'. Hunk: ctx a, +X, +Y, ctx b, ctx c.
    tmp.writeFile("a.txt", "a\nX\nY\nb\nc\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);
    const auto& hunk = d->hunks[0];

    // Find the line index of the added "X".
    int xIdx = -1;
    for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i)
    {
        if (hunk.lines[i].origin == gittide::DiffLineOrigin::Added && hunk.lines[i].text == "X")
        {
            xIdx = i;
            break;
        }
    }
    REQUIRE(xIdx >= 0);

    // Stage ONLY the "X" line.
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", 0, {xIdx}}).has_value());

    // Index vs HEAD: exactly one added line, and it is "X" (not Y).
    auto staged = repo->diff(gittide::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);
    int addedCount = 0;
    bool sawX = false, sawY = false;
    for (const auto& ln : staged->hunks[0].lines)
    {
        if (ln.origin == gittide::DiffLineOrigin::Added)
        {
            ++addedCount;
            if (ln.text == "X")
                sawX = true;
            if (ln.text == "Y")
                sawY = true;
        }
    }
    REQUIRE(addedCount == 1);
    REQUIRE(sawX);
    REQUIRE_FALSE(sawY);

    // Y is still pending in the worktree.
    auto unstaged = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.size() == 1);
}

TEST_CASE("staging two hunks of one file resolves both against one snapshot", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    std::string base, edited;
    for (int i = 1; i <= 30; ++i)
    {
        base += std::to_string(i) + "\n";
        edited += (i == 1 ? "ONE\n" : i == 15 ? "FIFTEEN\n" : i == 30 ? "THIRTY\n" : std::to_string(i) + "\n");
    }
    tmp.writeFile("a.txt", base);
    tmp.commitAll("init");
    tmp.writeFile("a.txt", edited);

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gittide::DiffTarget::WorktreeVsHead, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 3);

    // Hunk indices come from ONE diff snapshot, exactly as the commit checkboxes
    // collect them: staging hunk 0 first would renumber hunk 2 out of existence.
    const std::vector<gittide::StageSelection> sels{
        gittide::StageSelection{"a.txt", 0, {}},
        gittide::StageSelection{"a.txt", 2, {}},
    };
    auto r = repo->stage(sels);
    if (!r)
        WARN(r.error().message);
    REQUIRE(r.has_value());

    auto staged = repo->diff(gittide::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 2);
    // What stayed behind is the middle hunk, untouched.
    auto unstaged = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.size() == 1);
    REQUIRE(unstaged->hunks[0].lines[3].text == "15");
    REQUIRE(unstaged->hunks[0].lines[4].text == "FIFTEEN");
}

TEST_CASE("staging lines from two hunks of one file keeps both selections", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    std::string base, edited;
    for (int i = 1; i <= 30; ++i)
    {
        base += std::to_string(i) + "\n";
        edited += (i == 1 ? "ONE\nONE-B\n" : i == 30 ? "THIRTY\nTHIRTY-B\n" : std::to_string(i) + "\n");
    }
    tmp.writeFile("a.txt", base);
    tmp.commitAll("init");
    tmp.writeFile("a.txt", edited);

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto d = repo->diff(gittide::DiffTarget::WorktreeVsHead, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 2);

    // One added line out of each hunk (the '+' lines sit after the '-' line).
    auto addedIndex = [](const gittide::DiffHunk& h)
    {
        for (int i = 0; i < static_cast<int>(h.lines.size()); ++i)
            if (h.lines[i].origin == gittide::DiffLineOrigin::Added)
                return i;
        return -1;
    };
    const std::vector<gittide::StageSelection> sels{
        gittide::StageSelection{"a.txt", 0, {addedIndex(d->hunks[0])}},
        gittide::StageSelection{"a.txt", 1, {addedIndex(d->hunks[1])}},
    };
    auto r = repo->stage(sels);
    if (!r)
        WARN(r.error().message);
    REQUIRE(r.has_value());

    auto staged = repo->diff(gittide::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 2);
}

TEST_CASE("stage some lines of an untracked file", "[stage]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("seed.txt", "seed\n");
    tmp.commitAll("init");
    tmp.writeFile("new.txt", "a\nb\nc\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto d = repo->diff(gittide::DiffTarget::WorktreeVsHead, "new.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    // An untracked file has no index entry: the patch must be framed as an add.
    auto r = repo->stage(gittide::StageSelection{"new.txt", 0, {0, 1}});
    if (!r)
        WARN(r.error().message);
    REQUIRE(r.has_value());

    auto staged = repo->diff(gittide::DiffTarget::IndexVsHead, "new.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);
    REQUIRE(staged->hunks[0].lines.size() == 2);
    REQUIRE(staged->hunks[0].lines[0].text == "a");
    REQUIRE(staged->hunks[0].lines[1].text == "b");
}
