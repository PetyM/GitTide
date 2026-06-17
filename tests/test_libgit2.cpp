#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "gittide/libgit2context.hpp"

TEST_CASE("libgit2 initializes and reports a version", "[libgit2]")
{
    gittide::LibGit2Context ctx; // RAII init
    int major = 0, minor = 0, rev = 0;
    git_libgit2_version(&major, &minor, &rev);
    REQUIRE(major >= 1);
}
