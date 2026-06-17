#include "gittide/libgit2context.hpp"
#include <git2.h>

namespace gittide {
LibGit2Context::LibGit2Context()  { git_libgit2_init(); }
LibGit2Context::~LibGit2Context() { git_libgit2_shutdown(); }
}  // namespace gittide
