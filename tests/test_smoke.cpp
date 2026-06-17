#include <catch2/catch_test_macros.hpp>
#include "gittide/Version.hpp"

TEST_CASE("core version string is non-empty", "[smoke]") {
    REQUIRE_FALSE(gittide::kVersion.empty());
}
