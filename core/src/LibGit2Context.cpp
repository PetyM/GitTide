#include "gitgui/LibGit2Context.hpp"
#include <git2.h>

namespace gitgui {
LibGit2Context::LibGit2Context()  { git_libgit2_init(); }
LibGit2Context::~LibGit2Context() { git_libgit2_shutdown(); }
}  // namespace gitgui
