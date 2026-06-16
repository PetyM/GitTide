#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitError.hpp"

TEST_CASE("GitError carries code and message", "[error]") {
    gitgui::GitError e{-3, "not found"};
    REQUIRE(e.code == -3);
    REQUIRE(e.message == "not found");
}

TEST_CASE("Expected can hold a value or an error", "[error]") {
    gitgui::Expected<int> ok = 42;
    REQUIRE(ok.has_value());
    REQUIRE(*ok == 42);

    gitgui::Expected<int> bad = std::unexpected(gitgui::GitError{-1, "boom"});
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().message == "boom");
}
