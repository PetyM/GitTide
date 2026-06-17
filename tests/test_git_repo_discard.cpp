#include <catch2/catch_test_macros.hpp>
#include "gittide/GitRepo.hpp"
#include "support/TempRepo.hpp"
#include <fstream>
#include <sstream>

static std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("discard whole file restores committed content", "[discard]") {
    gittide::test::TempRepo tmp;
    tmp.write_file("a.txt", "orig\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "changed\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->discard(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    REQUIRE(read_file(tmp.path() / "a.txt") == "orig\n");
}

TEST_CASE("discard a hunk reverts only that region", "[discard]") {
    gittide::test::TempRepo tmp;
    tmp.write_file("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "ONE\n2\n3\n4\n5\n6\n7\n8\nNINE\n");

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
