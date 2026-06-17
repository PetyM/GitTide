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

// diff/stage/unstage/discard/commit are added in Task 4.

}  // namespace gitgui::ui
