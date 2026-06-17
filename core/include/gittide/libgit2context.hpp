#pragma once

namespace gittide {

// RAII wrapper around git_libgit2_init / git_libgit2_shutdown.
// Construct once near program start (and once per test that uses libgit2).
// Single-instance guard: neither copyable nor movable. (Copy deletion alone
// already suppresses implicit moves; moves are deleted explicitly for clarity,
// so the no-move intent is stated rather than inferred.)
class LibGit2Context
{
public:
    LibGit2Context();
    ~LibGit2Context();
    LibGit2Context(const LibGit2Context&)            = delete;
    LibGit2Context& operator=(const LibGit2Context&) = delete;
    LibGit2Context(LibGit2Context&&)                 = delete;
    LibGit2Context& operator=(LibGit2Context&&)      = delete;
};

} // namespace gittide
