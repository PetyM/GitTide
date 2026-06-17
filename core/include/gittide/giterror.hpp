#pragma once
#include <expected>
#include <string>

namespace gittide {

struct GitError
{
    int code = 0;        // libgit2 error code (git_error_code) or custom
    std::string message; // human-readable detail
};

template <typename T>
using Expected = std::expected<T, GitError>;

// Build a GitError from libgit2's thread-local error slot.
// CONTRACT: call this immediately after the failing libgit2 call, on the SAME
// thread, before issuing any other libgit2 call. The error slot is overwritten
// by the next libgit2 operation, so a delayed call captures a stale/unrelated
// error. `code` is the return code of the failed call and is forwarded verbatim.
GitError last_git_error(int code);

} // namespace gittide
