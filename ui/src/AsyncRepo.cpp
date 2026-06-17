#include "gittide/ui/AsyncRepo.hpp"

#include <mutex>
#include <utility>

#include <QtConcurrent>
#include <core/qcorofuture.h>

#include "gittide/GitRepo.hpp"

namespace gittide::ui {

struct AsyncRepo::Impl {
    explicit Impl(gittide::GitRepo r) : repo(std::move(r)) {}
    gittide::GitRepo repo;
    std::mutex mutex;  // serializes pool access to the non-thread-safe GitRepo
};

AsyncRepo::~AsyncRepo() = default;

gittide::Expected<AsyncRepo> AsyncRepo::open(const std::filesystem::path& path) {
    auto r = gittide::GitRepo::open(path);
    if (!r) return std::unexpected(r.error());
    return AsyncRepo(std::make_shared<Impl>(std::move(*r)));
}

QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> AsyncRepo::status() {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.status();
    });
}

QCoro::Task<gittide::Expected<gittide::DiffResult>> AsyncRepo::diff(
    gittide::DiffTarget target, std::filesystem::path file) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, target, file = std::move(file)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.diff(target, file);
    });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::stage(gittide::StageSelection sel) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.stage(sel);
    });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::unstage(gittide::StageSelection sel) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.unstage(sel);
    });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::discard(gittide::StageSelection sel) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.discard(sel);
    });
}

QCoro::Task<gittide::Expected<std::string>> AsyncRepo::commit(gittide::CommitRequest req) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, req = std::move(req)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.commit(req);
    });
}

QCoro::Task<gittide::Expected<std::vector<gittide::CommitNode>>> AsyncRepo::log(
    unsigned limit) {
    auto impl = impl_;
    co_return co_await QtConcurrent::run([impl, limit]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.log(limit);
    });
}

}  // namespace gittide::ui
