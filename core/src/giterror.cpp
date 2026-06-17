#include "gittide/giterror.hpp"

#include <git2.h>

namespace gittide {
GitError lastGitError(int code)
{
    const git_error* e = git_error_last();
    if (e && e->message)
        return GitError{code, e->message};
    return GitError{code, "unknown libgit2 error (code " + std::to_string(code) + ")"};
}
} // namespace gittide
