#pragma once
#include <filesystem>
#include <cstdint>

namespace gittide {

enum class StatusFlag : std::uint32_t {
    None          = 0,
    IndexNew      = 1 << 0,   // staged: new file
    IndexModified = 1 << 1,   // staged: modified
    IndexDeleted  = 1 << 2,   // staged: deleted
    WtNew         = 1 << 3,   // unstaged: untracked
    WtModified    = 1 << 4,   // unstaged: modified
    WtDeleted     = 1 << 5,   // unstaged: deleted
};

constexpr StatusFlag operator|(StatusFlag a, StatusFlag b) {
    return static_cast<StatusFlag>(static_cast<std::uint32_t>(a) |
                                   static_cast<std::uint32_t>(b));
}
constexpr StatusFlag& operator|=(StatusFlag& a, StatusFlag b) {
    a = a | b; return a;
}
constexpr StatusFlag operator&(StatusFlag a, StatusFlag b) {
    return static_cast<StatusFlag>(static_cast<std::uint32_t>(a) &
                                   static_cast<std::uint32_t>(b));
}
constexpr bool has_flag(StatusFlag value, StatusFlag flag) {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(flag)) != 0;
}

struct FileStatus {
    std::filesystem::path path;   // repo-relative
    StatusFlag flags = StatusFlag::None;
};

}  // namespace gittide
