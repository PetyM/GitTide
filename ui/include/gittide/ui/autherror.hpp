#pragma once
#include "gittide/giterror.hpp"

namespace gittide::ui {

/// True when a GitError denotes an authentication failure. Compares the libgit2
/// code numerically (GIT_EAUTH == -16; libgit2 is private to core, so this layer
/// never includes git2.h) with a best-effort message-substring fallback for
/// build configurations that remap the code.
bool isAuthError(const gittide::GitError& e);

} // namespace gittide::ui
