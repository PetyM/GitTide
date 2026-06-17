#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <qcorotask.h>

#include "gittide/Diff.hpp"
#include "gittide/FileStatus.hpp"
#include "gittide/GitError.hpp"

namespace gittide::ui {

// Async wrapper over a single GitRepo. Each call runs on Qt's global thread pool
// via QtConcurrent and is exposed as a co_await-able QCoro task. A per-repo mutex
// (held inside the worker lambda) serializes pool access so two awaited ops never
// touch the same git_repository concurrently — satisfying Core's one-owner rule.
//
// Move-only. The GitRepo + mutex live behind a shared_ptr so in-flight work stays
// valid even if the AsyncRepo is destroyed before the task completes.
class AsyncRepo {
public:
    static gittide::Expected<AsyncRepo> open(const std::filesystem::path& path);

    AsyncRepo(AsyncRepo&&) noexcept = default;
    AsyncRepo& operator=(AsyncRepo&&) noexcept = default;
    AsyncRepo(const AsyncRepo&) = delete;
    AsyncRepo& operator=(const AsyncRepo&) = delete;
    ~AsyncRepo();

    QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> status();
    QCoro::Task<gittide::Expected<gittide::DiffResult>> diff(
        gittide::DiffTarget target, std::filesystem::path file);
    QCoro::Task<gittide::Expected<void>> stage(gittide::StageSelection sel);
    QCoro::Task<gittide::Expected<void>> unstage(gittide::StageSelection sel);
    QCoro::Task<gittide::Expected<void>> discard(gittide::StageSelection sel);
    QCoro::Task<gittide::Expected<std::string>> commit(gittide::CommitRequest req);

private:
    struct Impl;
    explicit AsyncRepo(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<Impl> impl_;
};

}  // namespace gittide::ui
