# Code style

The coding standard for GitTide — naming, formatting, and the mandatory C++/Qt
rules every change must follow. Numbers like `(883)` are the rule IDs; keep them so rules can be cited in review.

Formatting is enforced by [`.clang-format`](../../../.clang-format) (root of the
repo);

> **Applies to existing code too** — but opportunistically. See
> [Reconciliation with existing code](#reconciliation-with-existing-code) at the
> end: the standard is authoritative for new code, and existing files are brought
> into conformance **when you touch them**, not in a big-bang reformat.

## Design principles

Apply these before reaching for the detailed rules:

- **KISS** — the simplest design that solves the problem. If a file starts to feel
  like spaghetti, don't hide it by saving lines; split it into smaller pieces
  `(701)`.
- **DRY** — one source of truth for each fact/behaviour; factor out duplication.
- **SOLID** — single-responsibility types, depend on abstractions (the `I`-prefixed
  interfaces `(811)`), keep the layering (see
  [`engineering.md`](engineering.md)).
- **YAGNI** — build what the current plan needs, not speculative generality. New
  scope is a [wish](../../wishlist/index.md) first.

## Formatting

The authoritative rules live in `.clang-format` (`BasedOnStyle: LLVM` with overrides). The essentials:

- **Braces on a new line (Allman)** `(701)` for functions, classes, structs,
  enums, and control statements (namespaces keep the brace on the same line).
- **Always use braces**, even for a single statement after `if`/`for`/`while`
  `(997)` — so adding a second statement never forces you to wrap existing code in
  a new block.
- 4-space indent, no tabs; column limit 130; pointer binds left (`int* p`);
  consecutive assignments aligned; includes regrouped (see
  [Includes](#includes)); namespace bodies not indented.

## Naming

- **Identifiers are English** — variable, type, function names, etc.
- **No unclear names** `(709)` — `temp` (temperature? temporary?) is banned. A name
  must say what it is.
- **Source file names are lowercase** `(743)` (e.g. `mainwindow.cpp`). Windows is
  case-insensitive, so mixed-case file names risk accidental overwrites.
- **Types** (class, struct, enum) are **PascalCase** `(743)` — `MainWindow`.
- **Constants and enum values** are **UPPER_SNAKE_CASE** `(743)` — `FILE_FAN_TYPE`.
- **Class member variables**: `m_` + camelCase `(743)` — `m_displayWidth`.
- **Struct member variables**: camelCase, no prefix `(743)` — `nodeId`.
- **Methods / functions**: camelCase `(743)` — `getInfo`.
- **Interface classes** (any pure-virtual method) take an `I` prefix `(811)` —
  `IRepoSource`.

## Mandatory rules

- **No custom macros; avoid using macros at all** where avoidable `(991)` — except
  debug macros (`qDebug`, `qCDebug`, …). Prefer inline functions and `const`
  variables. Macros are hard to debug and aren't namespaced (redefinition risk).
- **`static constexpr` (or at least `static const`) instead of `#define` and magic
  numbers** `(911)`. Prefer `static constexpr` (initialised in the header,
  compile-time) over `static const` (initialised in the `.cpp`).
- **Initialise everything** `(887)` — all local variables; in a constructor,
  every member not initialised at its declaration and lacking a default ctor.
- **Use `const` and `override` to the maximum** `(883)` — on methods, parameters,
  locals, and every overriding virtual. A `const` you can't accidentally mutate is
  one less thing to track when hunting a bug.
- **Pass parameters by `const` reference** wherever possible and efficient `(881)`
  (not for primitives, where it isn't).
- **No exceptions for control flow in Qt code** `(877)` — throwing out of a
  signal/slot invocation is undefined behaviour. (Core already uses
  `std::expected`; see [`engineering.md`](engineering.md).)
- **Never C-style arrays** `(863)` — use a container (`std::array`). Mind access:
  `at()` throws `std::out_of_range`; `operator[]` only asserts and is undefined
  past the end in release. A memory overwrite is the worst bug to find.
- **No C-style casts** `(857)` — use `static_cast`, `boost::polymorphic_downcast`,
  `qobject_cast`, or `dynamic_cast` (cheapest → most expensive).
- **A `mutable` member must be guarded by a mutex on *every* access** `(829)` —
  reads in `const` getters included. `const` must be thread-safe.
- **A virtual called from a constructor must be `final`** in this or a base class
  `(853)` — otherwise an override won't be reached from the base ctor.
- **Persisted enums get explicit values** `(839)` plus a warning not to change
  them — inserting a value in the middle would renumber the rest and corrupt the
  reading of older files.
- **No `using namespace` in a header** `(859)`.
- **A template declared in a `.h` must not have its implementation in a `.cpp`**
  `(727)` — anyone including the header needs the implementation to instantiate it.
- **Never put mutating code in an `assert`** `(827)` — it's gone in release builds.

## Comments

- **Doxygen-style** `(937)`. In `.cpp`, comment every non-obvious thing and any
  specialised (proprietary/domain) formula. Prefer self-documenting code: if a
  block does one specific job that needs explaining, extract it into a method or a
  named lambda instead of commenting it in place.
- **Don't comment overridden virtuals** `(929)` — document them once, in the base
  class.
- **TODOs need a ticket number** `(919)` — `TODO SA1-1680: description`. A personal
  `TODOMOJE` must never leave your feature branch.

## Includes

- **Headers declare, they don't define** `(983)` — no function bodies (templates
  excepted), including ctors/dtors even when the body looks empty (it still runs
  member ctors/dtors). Implementation detail belongs in the `.cpp`.
- **Template implementations may live in a `.inl`** `(977)` — other headers then
  include only the `.h`; the `.cpp` includes both `.h` and `.inl`. Keeps template
  bodies out of unrelated translation units.
- **`<header>` for standard/system, `"path/from/project/root"` for our code**
  `(971)`. Always write the full path from the project root (avoids file-name
  clashes within the project). The one exception: a `.cpp` includes its own header
  with no path (they sit in the same directory).
- **Include order from most specific to most general** `(967)`: the file's own
  header → this subproject's headers → other subprojects → third-party → system.
  (`.clang-format` regroups within this scheme.)
- **Match case** in `#include` directives, library names, and paths `(947)`.
- **Include guards name the project or namespace** `(953)`.
- **Headers are self-sufficient** `(941)` — each forward-declares or includes every
  dependency, always preferring a **forward declaration** over an `#include` when
  possible. Minimal includes keep compile times and IntelliSense fast and avoid
  bogus compile dependencies.

## Good habits

- **`private using BaseClass = Parent;`** in every singly-derived class `(823)` —
  then use `BaseClass` for ctor delegation and base-implementation calls (like
  `super`/`base`).
- **Write `virtual` and `override`** on virtual functions and dtors in derived
  classes `(821)`, for readability.
- **`auto` with judgement** `(809)` — great for iterators, heavy templates, or when
  spelling the type twice adds nothing (`auto* o = boost::polymorphic_downcast<Object*>(obj)`);
  not where an explicit type aids the reader.
- **RAII for locking and temporary setting changes** `(787)` — e.g.
  `QMutexLocker`. The unlock/restore then runs even on an early/exception exit.
- **Avoid `new`** `(773)` — prefer smart pointers (RAII again).
- **`sizeof(varname)`, not `sizeof(type)`** `(761)`.
- **Prefer std containers over Qt containers** `(691)` — no need for an extra
  dependency when std does it as well or better (structured bindings on
  `std::map` vs `QMap`).
- **Prefer `qCDebug`/`qCWarning`/… over `qDebug`/`std::cerr`** `(733)` — log levels
  stay adjustable.
- **`boost::polymorphic_downcast` instead of `assert(dynamic_cast) + static_cast`**
  `(757)`.
- **Close namespaces with a comment** `(751)` — `} // namespace gittide`.
- **Iterable enums** `(797)`: prefix each value with the type name (plain-enum
  values act as bare constants) and bracket with `__BEGIN`/`__END` sentinels plus a
  `static_assert` on the count so a later edit is forced to revisit related code.
  **Non-iterable enums**: `enum class`, accessed as `EType::NODE`.

```cpp
enum EType {
    ETYPE__BEGIN = 0,
    ETYPE_NODE = ETYPE__BEGIN,
    ETYPE_LINE,
    ETYPE__END
};
static_assert(ETYPE__END - ETYPE__BEGIN == 2, "add new type handler here");
```

## Conformance

The codebase **conforms to this standard**: source files are lowercase, class
members use the `m_camelCase` prefix, free functions and methods are camelCase,
and formatting is Allman (enforced by `.clang-format`). The standard is
authoritative for **all** code — keep new code conformant; do not reintroduce the
old conventions (trailing-underscore members, snake_case functions, mixed-case
file names, K&R braces).

**Test layer exception.** Test *case* names (Qt Test slots and Catch2 `TEST_CASE`
identifiers) and test-local helper functions keep a descriptive **snake_case**
form — the convention shown throughout
[`testing.md`](testing.md). The `m_…` member and camelCase function rules still
apply to production code (`core/`, `ui/`, `app/`) and to shared test
infrastructure such as `TempRepo`.

**Renaming a file or a member later** — follow `(739)`: **split the rename and the
content change into separate commits** (rename first, via `git mv`). Git only
records a rename when the content is otherwise stable, so doing both at once loses
the file's history. A **case-only** filename change (`Foo.cpp` → `foo.cpp`) on a
case-insensitive filesystem (macOS, Windows) is not reliably picked up by a single
`git mv` — go through a temporary name (`git mv Foo.cpp foo.cpp.tmp && git mv
foo.cpp.tmp foo.cpp`), or a later `git add -A` will silently revert it.
