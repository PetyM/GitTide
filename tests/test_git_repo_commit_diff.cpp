#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
bool has(const std::vector<gittide::FileStatus>& v, const std::string& p)
{
    return std::any_of(v.begin(), v.end(),
                       [&](const auto& f) { return f.path.generic_string() == p; });
}
}

TEST_CASE("commitFiles lists files added in a commit vs its parent", "[commitfiles]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("c1");
    tmp.writeFile("b.txt", "two\n");          // add a new file in c2
    tmp.commitAll("c2");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto history = repo->log();
    REQUIRE(history.has_value());
    REQUIRE(history->size() == 2);
    const std::string c2 = history->front().oid; // newest first

    auto files = repo->commitFiles(c2);
    REQUIRE(files.has_value());
    REQUIRE(has(*files, "b.txt"));
    REQUIRE_FALSE(has(*files, "a.txt"));      // a.txt unchanged in c2
    auto it = std::find_if(files->begin(), files->end(),
                           [](const auto& f) { return f.path.generic_string() == "b.txt"; });
    REQUIRE(hasFlag(it->flags, StatusFlag::IndexNew));
}

TEST_CASE("commitFiles treats the root commit's files as added", "[commitfiles]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("root");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    const std::string root = repo->log()->front().oid;

    auto files = repo->commitFiles(root);
    REQUIRE(files.has_value());
    REQUIRE(has(*files, "a.txt"));
    REQUIRE(hasFlag((*files)[0].flags, StatusFlag::IndexNew));
}
