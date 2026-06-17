#include <catch2/catch_test_macros.hpp>
#include "gittide/gitrepo.hpp"
#include "gittide/pathutil.hpp"
#include "support/temprepo.hpp"
#include <git2.h>

TEST_CASE("GitRepo::commit creates a commit from the staged index", "[commit]") {
    gittide::test::TempRepo tmp;
    tmp.set_identity("Ada", "ada@example.com");
    tmp.write_file("a.txt", "hello\n");
    tmp.commit_all("init");                 // first commit (HEAD now exists)
    tmp.write_file("a.txt", "hello\nworld\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto oid = repo->commit(gittide::CommitRequest{"second"});
    REQUIRE(oid.has_value());
    REQUIRE(oid->size() == 40);             // full sha-1 hex

    // The new commit is HEAD, has the right message and author.
    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gittide::to_git_path(tmp.path()).c_str()) == 0);
    git_oid head; REQUIRE(git_reference_name_to_id(&head, r, "HEAD") == 0);
    git_commit* c = nullptr; REQUIRE(git_commit_lookup(&c, r, &head) == 0);
    REQUIRE(std::string(git_commit_message(c)) == "second");
    REQUIRE(std::string(git_commit_author(c)->name) == "Ada");
    REQUIRE(git_commit_parentcount(c) == 1);
    git_commit_free(c);
    git_repository_free(r);
}
