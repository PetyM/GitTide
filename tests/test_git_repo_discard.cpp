#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

static std::string read_file(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("discard whole file restores committed content", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "changed\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->discard(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    REQUIRE(read_file(tmp.path() / "a.txt") == "orig\n");
}

TEST_CASE("discard untracked file removes it from the worktree", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("keep.txt", "x\n");
    tmp.commitAll("init");
    tmp.writeFile("new.txt", "fresh\n"); // untracked, never staged

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->discard(gittide::StageSelection{"new.txt", std::nullopt, {}}).has_value());

    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "new.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "keep.txt")); // untouched
}

TEST_CASE("discard staged new file unstages and removes it", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("keep.txt", "x\n");
    tmp.commitAll("init");
    tmp.writeFile("added.txt", "fresh\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"added.txt", std::nullopt, {}}).has_value());
    REQUIRE(repo->discard(gittide::StageSelection{"added.txt", std::nullopt, {}}).has_value());

    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "added.txt"));

    auto st = repo->status();
    REQUIRE(st.has_value());
    for (const auto& fs : *st)
        REQUIRE(fs.path != std::filesystem::path("added.txt")); // gone from index too
}

TEST_CASE("discard a hunk reverts only that region", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "ONE\n2\n3\n4\n5\n6\n7\n8\nNINE\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 2);

    // Discard only the first hunk (the ONE change); NINE stays.
    REQUIRE(repo->discard(gittide::StageSelection{"a.txt", 0, {}}).has_value());

    std::string after = read_file(tmp.path() / "a.txt");
    REQUIRE(after.find("ONE") == std::string::npos);  // reverted
    REQUIRE(after.find("NINE") != std::string::npos); // kept
    REQUIRE(after.substr(0, 2) == "1\n");
}

TEST_CASE("discardAll resets tracked and deletes untracked", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("tracked.txt", "orig\n");
    tmp.commitAll("init");
    tmp.writeFile("tracked.txt", "changed\n"); // modified tracked
    tmp.writeFile("untracked.txt", "new\n");    // untracked

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->discardAll().has_value());

    REQUIRE(read_file(tmp.path() / "tracked.txt") == "orig\n"); // reset
    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "untracked.txt")); // removed

    auto st = repo->status();
    REQUIRE(st.has_value());
    REQUIRE(st->empty()); // fully clean tree
}

TEST_CASE("discardAll drops a staged new file", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("keep.txt", "x\n");
    tmp.commitAll("init");
    tmp.writeFile("staged.txt", "fresh\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"staged.txt", std::nullopt, {}}).has_value());
    REQUIRE(repo->discardAll().has_value());

    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "staged.txt"));
    REQUIRE(repo->status().value().empty());
}
