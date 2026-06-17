#include "gittide/PathUtil.hpp"

namespace gittide {

std::string to_git_path(const std::filesystem::path& p) {
    // generic_u8string() yields UTF-8 with '/' separators on every platform.
    auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path from_git_path(std::string_view git_path) {
    std::u8string u8(git_path.begin(), git_path.end());
    return std::filesystem::path(u8).lexically_normal();
}

}  // namespace gittide
