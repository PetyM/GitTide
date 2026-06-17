#pragma once
#include <filesystem>
#include <string_view>
#include "gitgui/LibGit2Context.hpp"

struct git_repository;

namespace gitgui::test {

// Creates a unique temporary git repository under temp_directory_path().
// Removes the directory on destruction. Owns a LibGit2Context for its lifetime.
class TempRepo {
public:
    TempRepo();
    ~TempRepo();
    TempRepo(const TempRepo&) = delete;
    TempRepo& operator=(const TempRepo&) = delete;

    const std::filesystem::path& path() const { return dir_; }

    // Write (or overwrite) a file at a repo-relative path.
    void write_file(std::string_view rel_path, std::string_view contents);

    // Stage all changes and create a commit with a fixed test author.
    void commit_all(std::string_view message);

    // Write user.name / user.email into the repo's git config.
    void set_identity(std::string_view name, std::string_view email);

private:
    LibGit2Context ctx_;
    std::filesystem::path dir_;
    git_repository* repo_ = nullptr;
};

}  // namespace gitgui::test
