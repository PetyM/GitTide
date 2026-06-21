#pragma once

#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace gittide {

/// Severity ladder for a log record, lowest to highest. Maps onto Qt's QtMsgType
/// at the app bridge (Trace+Debug -> QtDebugMsg, Info -> QtInfoMsg,
/// Warning -> QtWarningMsg, Error -> QtCriticalMsg).
///
/// Logging is the *diagnostic* channel; it does not replace the error-as-value
/// path. An Error-level log usually accompanies a returned GitError rather than
/// standing in for it.
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

/// Canonical category names, shared by every layer. A category groups related
/// log lines so verbosity can be raised for one area while the rest stay quiet.
/// The "gittide." prefix namespaces them for Qt's QT_LOGGING_RULES so a rule
/// never collides with another library's category.
namespace logcat {
inline constexpr std::string_view GIT   = "gittide.git";   ///< libgit2 operations (core)
inline constexpr std::string_view REPO  = "gittide.repo";  ///< repo / project persistence (core)
inline constexpr std::string_view ASYNC = "gittide.async"; ///< worker / refresh cascade (ui bridge)
inline constexpr std::string_view AUTH  = "gittide.auth";  ///< credential selection (core / ui)
inline constexpr std::string_view UI    = "gittide.ui";    ///< view-models / QML
inline constexpr std::string_view APP   = "gittide.app";   ///< startup / composition
} // namespace logcat

/// The diagnostic sink, installed once by the composition layer (app).
///
/// - `write` delivers a fully-formed record.
/// - `enabled` is a cheap gate so a disabled Trace/Debug line is never formatted.
///
/// Both run on worker threads, so both must be thread-safe. The struct is kept
/// Qt-free (std types only) so core never depends on Qt; the app wires these to
/// Qt's QLoggingCategory machinery at startup. (The emit member is named `write`,
/// not `emit`, because Qt defines `emit` as a macro in downstream consumers.)
struct LogBackend
{
    std::function<void(LogLevel level, std::string_view category, std::string_view message)> write;
    std::function<bool(LogLevel level, std::string_view category)>                            enabled;
};

/// Install (or replace) the process-wide log backend. Call once at startup,
/// before worker threads run. A default-constructed backend silences logging
/// (the default state until the app wires one). Thread-safe.
void setLogBackend(LogBackend backend);

/// True when a record at `level` for `category` would be emitted. Cheap and
/// callable from any thread. False when no backend (or no `enabled` predicate)
/// is installed.
bool logEnabled(LogLevel level, std::string_view category);

/// Emit one already-formatted record. Prefer logf() so the message is built only
/// when the record is actually enabled. Thread-safe.
void logMessage(LogLevel level, std::string_view category, std::string_view message);

/// Format-and-log. Gated on logEnabled() so the std::format cost is paid only
/// when the record will be emitted.
template <typename... Args>
void logf(LogLevel level, std::string_view category, std::format_string<Args...> fmt, Args&&... args)
{
    if (logEnabled(level, category))
        logMessage(level, category, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace gittide
