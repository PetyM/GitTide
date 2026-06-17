#include <catch2/catch_test_macros.hpp>

#include "gittide/diffengine.hpp"

using namespace gittide;

static DiffHunk sample_hunk()
{
    // Original file: a / b / c    New file: a / B2 / c
    // Lines: ctx 'a', -'b', +'B2', ctx 'c'
    DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 3;
    h.newStart = 1;
    h.newLines = 3;
    h.lines    = {
        {DiffLineOrigin::Context, 1, 1, "a"},
        {DiffLineOrigin::Removed, 2, -1, "b"},
        {DiffLineOrigin::Added, -1, 2, "B2"},
        {DiffLineOrigin::Context, 3, 3, "c"},
    };
    return h;
}

TEST_CASE("build_patch whole hunk (forward) round-trips structure", "[patch]")
{
    StageSelection sel{"f.txt", 0, {}}; // whole hunk
    std::string p = build_patch("f.txt", sample_hunk(), sel, /*reverse=*/false);

    REQUIRE(p.find("diff --git a/f.txt b/f.txt") != std::string::npos);
    REQUIRE(p.find("--- a/f.txt") != std::string::npos);
    REQUIRE(p.find("+++ b/f.txt") != std::string::npos);
    REQUIRE(p.find("@@ -1,3 +1,3 @@") != std::string::npos);
    REQUIRE(p.find("\n a\n") != std::string::npos); // context
    REQUIRE(p.find("\n-b\n") != std::string::npos);
    REQUIRE(p.find("\n+B2\n") != std::string::npos);
}

TEST_CASE("build_patch unselected added line is dropped, removed becomes context", "[patch]")
{
    // Select only the removed line (index 1). The added line (index 2) is NOT
    // selected -> dropped. Result: a / -b / c  => oldLines 3, newLines 2.
    StageSelection sel{"f.txt", 0, {1}};
    std::string p = build_patch("f.txt", sample_hunk(), sel, /*reverse=*/false);

    REQUIRE(p.find("@@ -1,3 +1,2 @@") != std::string::npos);
    REQUIRE(p.find("\n-b\n") != std::string::npos);
    REQUIRE(p.find("+B2") == std::string::npos); // dropped
}

TEST_CASE("build_patch reverse swaps + and -", "[patch]")
{
    StageSelection sel{"f.txt", 0, {}};
    std::string p = build_patch("f.txt", sample_hunk(), sel, /*reverse=*/true);
    // Reversed: the '+B2' becomes '-B2', the '-b' becomes '+b'.
    REQUIRE(p.find("\n-B2\n") != std::string::npos);
    REQUIRE(p.find("\n+b\n") != std::string::npos);
    REQUIRE(p.find("@@ -1,3 +1,3 @@") != std::string::npos);
}

TEST_CASE("build_patch emits no-newline marker for an EOF line", "[patch]")
{
    DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 1;
    h.newStart = 1;
    h.newLines = 1;
    // Old 'x' (no trailing newline) -> new 'y' (no trailing newline).
    DiffLine removed{DiffLineOrigin::Removed, 1, -1, "x"};
    removed.noNewline = true;
    DiffLine added{DiffLineOrigin::Added, -1, 1, "y"};
    added.noNewline = true;
    h.lines         = {removed, added};

    StageSelection sel{"f.txt", 0, {}};
    std::string p = build_patch("f.txt", h, sel, /*reverse=*/false);

    REQUIRE(p.find("\n-x\n\\ No newline at end of file\n") != std::string::npos);
    REQUIRE(p.find("\n+y\n\\ No newline at end of file\n") != std::string::npos);
}
