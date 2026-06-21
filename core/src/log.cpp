#include "gittide/log.hpp"

#include <memory>
#include <mutex>

namespace gittide {

namespace {

// The backend is installed once at startup, then read on many worker threads.
// A shared_ptr behind a mutex keeps an in-flight emit() valid even if the
// backend is replaced concurrently; the snapshot is copied out under the lock so
// emit()/enabled() themselves run unlocked. (std::atomic<std::shared_ptr> is not
// available on every standard library we target, so a small mutex is used.)
struct BackendSlot
{
    std::mutex                        mutex;
    std::shared_ptr<const LogBackend> backend;
};

BackendSlot& slot()
{
    static BackendSlot s;
    return s;
}

std::shared_ptr<const LogBackend> currentBackend()
{
    BackendSlot&           slotRef = slot();
    const std::scoped_lock lock(slotRef.mutex);
    return slotRef.backend;
}

} // namespace

void setLogBackend(LogBackend backend)
{
    BackendSlot&           slotRef = slot();
    const std::scoped_lock lock(slotRef.mutex);
    slotRef.backend = std::make_shared<const LogBackend>(std::move(backend));
}

bool logEnabled(LogLevel level, std::string_view category)
{
    const std::shared_ptr<const LogBackend> backend = currentBackend();
    return backend && backend->enabled && backend->enabled(level, category);
}

void logMessage(LogLevel level, std::string_view category, std::string_view message)
{
    const std::shared_ptr<const LogBackend> backend = currentBackend();
    if (backend && backend->write)
        backend->write(level, category, message);
}

} // namespace gittide
