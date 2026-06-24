#include "gittide/rebase.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("RebaseTodo and extended RebaseState have sane defaults", "[rebase-i]")
{
    gittide::RebaseTodo todo;
    REQUIRE(todo.base.empty());
    REQUIRE(todo.entries.empty());

    gittide::RebaseTodoEntry e{gittide::RebaseAction::Squash, "abc"};
    REQUIRE(e.action == gittide::RebaseAction::Squash);
    REQUIRE(e.oid == "abc");

    gittide::RebaseState st;
    REQUIRE_FALSE(st.interactive);
    REQUIRE(st.pause == gittide::RebasePause::None);
    REQUIRE(st.messagePrefill.empty());

    gittide::RebaseOutcome out;
    REQUIRE_FALSE(out.conflicted);
    REQUIRE(out.pause == gittide::RebasePause::None);
}
