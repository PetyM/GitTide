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
