#include "gitgui/ui/AsyncRepo.hpp"

#include <mutex>
#include <utility>

#include <QtConcurrent>
#include <core/qcorofuture.h>

#include "gitgui/GitRepo.hpp"

namespace gitgui::ui {

struct AsyncRepo::Impl {
    explicit Impl(gitgui::GitRepo r) : repo(std::move(r)) {}
    gitgui::GitRepo repo;
    std::mutex mutex;  // serializes pool access to the non-thread-safe GitRepo
};

AsyncRepo::~AsyncRepo() = default;

gitgui::Expected<AsyncRepo> AsyncRepo::open(const std::filesystem::path& path) {
    auto r = gitgui::GitRepo::open(path);
    if (!r) return std::unexpected(r.error());
    return AsyncRepo(std::make_shared<Impl>(std::move(*r)));
}

QCoro::Task<gitgui::Expected<std::vector<gitgui::FileStatus>>> AsyncRepo::status() {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.status();
    });
}

QCoro::Task<gitgui::Expected<gitgui::DiffResult>> AsyncRepo::diff(
    gitgui::DiffTarget target, std::filesystem::path file) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, target, file = std::move(file)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.diff(target, file);
    });
}

QCoro::Task<gitgui::Expected<void>> AsyncRepo::stage(gitgui::StageSelection sel) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.stage(sel);
    });
}

QCoro::Task<gitgui::Expected<void>> AsyncRepo::unstage(gitgui::StageSelection sel) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.unstage(sel);
    });
}

QCoro::Task<gitgui::Expected<void>> AsyncRepo::discard(gitgui::StageSelection sel) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.discard(sel);
    });
}

QCoro::Task<gitgui::Expected<std::string>> AsyncRepo::commit(gitgui::CommitRequest req) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, req = std::move(req)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.commit(req);
    });
}

}  // namespace gitgui::ui
