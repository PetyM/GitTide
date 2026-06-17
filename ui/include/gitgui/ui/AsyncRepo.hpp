#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <qcorotask.h>

#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"
#include "gitgui/GitError.hpp"

namespace gitgui::ui {

// Async wrapper over a single GitRepo. Each call runs on Qt's global thread pool
// via QtConcurrent and is exposed as a co_await-able QCoro task. A per-repo mutex
// (held inside the worker lambda) serializes pool access so two awaited ops never
// touch the same git_repository concurrently — satisfying Core's one-owner rule.
//
// Move-only. The GitRepo + mutex live behind a shared_ptr so in-flight work stays
// valid even if the AsyncRepo is destroyed before the task completes.
class AsyncRepo {
public:
    static gitgui::Expected<AsyncRepo> open(const std::filesystem::path& path);

    AsyncRepo(AsyncRepo&&) noexcept = default;
    AsyncRepo& operator=(AsyncRepo&&) noexcept = default;
    AsyncRepo(const AsyncRepo&) = delete;
    AsyncRepo& operator=(const AsyncRepo&) = delete;
    ~AsyncRepo();

    QCoro::Task<gitgui::Expected<std::vector<gitgui::FileStatus>>> status();
    QCoro::Task<gitgui::Expected<gitgui::DiffResult>> diff(
        gitgui::DiffTarget target, std::filesystem::path file);
    QCoro::Task<gitgui::Expected<void>> stage(gitgui::StageSelection sel);
    QCoro::Task<gitgui::Expected<void>> unstage(gitgui::StageSelection sel);
    QCoro::Task<gitgui::Expected<void>> discard(gitgui::StageSelection sel);
    QCoro::Task<gitgui::Expected<std::string>> commit(gitgui::CommitRequest req);

private:
    struct Impl;
    explicit AsyncRepo(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<Impl> impl_;
};

}  // namespace gitgui::ui
