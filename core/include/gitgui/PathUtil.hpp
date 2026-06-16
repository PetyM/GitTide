#pragma once
#include <filesystem>
#include <string>

namespace gitgui {

// Convert a filesystem path to the form libgit2 expects:
// UTF-8 bytes with forward-slash separators. Use this at EVERY libgit2 call.
std::string to_git_path(const std::filesystem::path& p);

// Convert a UTF-8 forward-slash path (as returned by libgit2) back to a
// native std::filesystem::path.
std::filesystem::path from_git_path(std::string_view git_path);

}  // namespace gitgui
