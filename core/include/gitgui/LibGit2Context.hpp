#pragma once

namespace gitgui {

// RAII wrapper around git_libgit2_init / git_libgit2_shutdown.
// Construct once near program start (and once per test that uses libgit2).
class LibGit2Context {
public:
    LibGit2Context();
    ~LibGit2Context();
    LibGit2Context(const LibGit2Context&) = delete;
    LibGit2Context& operator=(const LibGit2Context&) = delete;
};

}  // namespace gitgui
