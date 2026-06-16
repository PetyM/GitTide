#pragma once
#include <expected>
#include <string>

namespace gitgui {

struct GitError {
    int code = 0;          // libgit2 error code (git_error_code) or custom
    std::string message;   // human-readable detail
};

template <typename T>
using Expected = std::expected<T, GitError>;

// Build a GitError from the current libgit2 thread-local error after a failed call.
GitError last_git_error(int code);

}  // namespace gitgui
