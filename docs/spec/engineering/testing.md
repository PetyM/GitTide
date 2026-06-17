# Testing

How tests are written and run in GitTide. **TDD is the default** — write the
failing test first (see [workflow](../../workflow.md)). Commands to run the
suites are in [`README.md`](../../../README.md#build--test); this is about *how
the tests are structured* and how to add one.

## Two suites

- **Core — Catch2** (`gittide_core_tests`). Pure C++, no display. Each
  `tests/test_*.cpp` is a normal translation unit listed in
  [`tests/CMakeLists.txt`](../../../tests/CMakeLists.txt); `catch_discover_tests`
  registers one ctest entry per `TEST_CASE`, so you can run a single case by name.
- **UI — Qt Test** (`gittide_ui_tests`). One aggregated binary, run headless
  (`QT_QPA_PLATFORM=offscreen`). Covers models, controllers, session/window
  bookkeeping, and widget smoke tests.

## What to test where

- **Core logic** (git ops, diff parsing, partial-staging patch synthesis, graph
  lane layout, the project store) → Catch2, thoroughly, with no Qt. Partial-staging
  logic deliberately lives in Core so it stays Catch2-testable (see
  [`decisions.md`](../../decisions.md)).
- **Controllers, models, session, window bookkeeping** → QtTest, headless.
- **Widgets** → smoke tests (construction) plus signal/slot wiring.

## Core tests: Catch2 + `TempRepo`

Use [`TempRepo`](../../../tests/support/temprepo.hpp) (`gittide::test`) for a
throwaway repository — it creates a unique repo under the temp dir, cleans it up
on destruction, and owns a `LibGit2Context`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "support/temprepo.hpp"

TEST_CASE("status reports a new file as untracked", "[gitrepo]") {
    gittide::test::TempRepo repo;
    repo.writeFile("a.txt", "hello");
    auto r = gittide::GitRepo::open(repo.path());
    REQUIRE(r);
    // … assert on r->status()
}
```

`TempRepo` also offers `commitAll(msg)` and `setIdentity(name, email)`. There is
**no network in tests** — clone tests clone from a local `file://` temp repo.
Tests that include `<git2.h>` directly rely on the target's explicit libgit2 link
(already wired).

## UI tests: the `#include` runner (read this before adding one)

UI tests are **not** separate translation units. Each `tests/ui/test_<name>.cpp`
is a `QObject` with `private slots`, ending in `#include "test_<name>.moc"`; they
are `#include`-d into [`tests/ui/main.cpp`](../../../tests/ui/main.cpp), which
runs each class via `QTest::qExec`. Skeleton:

```cpp
#include <QtTest>
#include "gittide/ui/theme.hpp"

class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void dark_theme_has_brand_tokens() {
        const gittide::ui::Theme t = gittide::ui::darkTheme();
        QVERIFY(t.dark);
        QCOMPARE(t.accent, QStringLiteral("#22D3EE"));
    }
};

#include "test_theme.moc"
```

**Adding a UI test takes TWO edits — miss either and it compiles but silently
runs zero tests, with no warning:**

1. Add the file to `gittide_ui_test_sources` in
   [`tests/CMakeLists.txt`](../../../tests/CMakeLists.txt) (it's `HEADER_FILE_ONLY`
   so AUTOMOC scans its `Q_OBJECT`).
2. In `tests/ui/main.cpp`: add `#include "test_<name>.cpp"` **and** a
   `QTest::qExec` block in `main()`.
