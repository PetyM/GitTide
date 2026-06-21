#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "gittide/log.hpp"

using namespace gittide;

namespace {

struct CapturedRecord
{
    LogLevel    level = LogLevel::Trace;
    std::string category;
    std::string message;
};

// A backend that records everything and is enabled at or above a threshold.
LogBackend recordingBackend(std::vector<CapturedRecord>& sink, LogLevel threshold)
{
    return LogBackend{
        .write =
            [&sink](LogLevel level, std::string_view category, std::string_view message)
        { sink.push_back(CapturedRecord{level, std::string(category), std::string(message)}); },
        .enabled = [threshold](LogLevel level, std::string_view) { return level >= threshold; },
    };
}

} // namespace

TEST_CASE("with no backend installed, logging is silent and disabled", "[log]")
{
    setLogBackend({}); // clear any backend a previous case may have left
    REQUIRE_FALSE(logEnabled(LogLevel::Error, logcat::GIT));
    // Must be a harmless no-op, not a crash, when nothing is wired.
    logMessage(LogLevel::Error, logcat::GIT, "dropped on the floor");
    logf(LogLevel::Error, logcat::GIT, "also {}", "dropped");
}

TEST_CASE("logf routes level, category and formatted message to the backend", "[log]")
{
    std::vector<CapturedRecord> records;
    setLogBackend(recordingBackend(records, LogLevel::Trace));

    logf(LogLevel::Warning, logcat::GIT, "open {} failed: {}", "repo", 42);

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].level == LogLevel::Warning);
    REQUIRE(records[0].category == std::string(logcat::GIT));
    REQUIRE(records[0].message == "open repo failed: 42");

    setLogBackend({});
}

TEST_CASE("logf is gated: a disabled record is never emitted", "[log]")
{
    std::vector<CapturedRecord> records;
    setLogBackend(recordingBackend(records, LogLevel::Warning));

    REQUIRE_FALSE(logEnabled(LogLevel::Debug, logcat::REPO));
    REQUIRE(logEnabled(LogLevel::Error, logcat::REPO));

    logf(LogLevel::Debug, logcat::REPO, "too chatty");
    logf(LogLevel::Error, logcat::REPO, "kept");

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].message == "kept");

    setLogBackend({});
}
