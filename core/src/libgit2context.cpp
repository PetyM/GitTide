#include "gittide/libgit2context.hpp"

#include <git2.h>

namespace gittide {
LibGit2Context::LibGit2Context()
{
    git_libgit2_init();

    // Bound the HTTP(S) transport so an unreachable server (e.g. an internal
    // remote while off-VPN) fails fast instead of hanging forever. These apply
    // to libgit2's built-in HTTP transport only — SSH (libssh2) is unaffected,
    // which is why the ui layer adds a watchdog/cancel around network ops.
    // Options exist since libgit2 1.7; guard so older headers still compile.
#if LIBGIT2_VER_MAJOR > 1 || (LIBGIT2_VER_MAJOR == 1 && LIBGIT2_VER_MINOR >= 7)
    git_libgit2_opts(GIT_OPT_SET_SERVER_CONNECT_TIMEOUT, 10000); // 10 s to connect
    git_libgit2_opts(GIT_OPT_SET_SERVER_TIMEOUT, 30000);         // 30 s per read/write
#endif
}
LibGit2Context::~LibGit2Context()
{
    git_libgit2_shutdown();
}
} // namespace gittide
