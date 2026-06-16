#include "gitgui/GitError.hpp"
#include <git2.h>

namespace gitgui {
GitError last_git_error(int code) {
    const git_error* e = git_error_last();
    return GitError{code, e && e->message ? e->message : "unknown libgit2 error"};
}
}  // namespace gitgui
