#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"

using gitgui::DiffTarget;
using gitgui::DiffLineOrigin;

TEST_CASE("GitRepo::diff WorktreeVsIndex shows unstaged edit", "[diff]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "x\ny\nz\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "x\nY2\nz\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    bool has_added = false, has_removed = false;
    for (const auto& ln : d->hunks[0].lines) {
        has_added   |= ln.origin == DiffLineOrigin::Added;
        has_removed |= ln.origin == DiffLineOrigin::Removed;
    }
    REQUIRE(has_added);
    REQUIRE(has_removed);
}

TEST_CASE("GitRepo::diff IndexVsHead is empty with nothing staged", "[diff]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "x\n");
    tmp.commit_all("init");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.empty());
}
