#pragma once

#include <QLoggingCategory>
#include <QObject>
#include <QString>

#include <string_view>

namespace gittide::ui {

// The Qt logging categories behind GitTide's coherent taxonomy (see
// gittide::logcat for the canonical names). Use these with qCDebug/qCWarning/...
// in ui/app code; core code logs through the Qt-free gittide::logf facade, which
// is bridged onto these categories by installCoreLogBridge().
Q_DECLARE_LOGGING_CATEGORY(catGit)
Q_DECLARE_LOGGING_CATEGORY(catRepo)
Q_DECLARE_LOGGING_CATEGORY(catAsync)
Q_DECLARE_LOGGING_CATEGORY(catAuth)
Q_DECLARE_LOGGING_CATEGORY(catUi)
Q_DECLARE_LOGGING_CATEGORY(catApp)

/// Map a category name to its QLoggingCategory. Accepts both the canonical
/// "gittide.git" form and the short "git" form; an unknown name falls back to the
/// "ui" category. The reference is to a process-static object, valid for the
/// program's lifetime.
const QLoggingCategory& categoryFor(std::string_view name);

/// Wire core's Qt-free LogBackend onto Qt's category machinery: core records
/// route to the matching QLoggingCategory (honouring QT_LOGGING_RULES) and the
/// enabled gate consults that category. Call once at startup.
void installCoreLogBridge();

/// Install a qInstallMessageHandler that writes every Qt (and bridged core)
/// message to stderr and, when `logDir` is non-empty, to a rotating log file
/// (`gittide.log`, rolled to `gittide.log.1` past `maxBytes`) under that
/// directory. Returns the previously installed handler so a caller can restore it.
QtMessageHandler installMessageHandler(const QString& logDir, qint64 maxBytes = 5 * 1024 * 1024);

/// App convenience: bridge core onto Qt and install the message handler logging
/// into `logDir`.
void installLogging(const QString& logDir);

/// QML-facing logger, exposed as the `log` context property so QML/JS diagnostics
/// flow through the same categories and levels as the rest of the app instead of
/// a stray console.log. The `category` argument is a short ("ui", "git") or
/// canonical ("gittide.ui") category name.
class QmlLog : public QObject
{
    Q_OBJECT
public:
    explicit QmlLog(QObject* parent = nullptr);

    Q_INVOKABLE void trace(const QString& category, const QString& message) const;
    Q_INVOKABLE void debug(const QString& category, const QString& message) const;
    Q_INVOKABLE void info(const QString& category, const QString& message) const;
    Q_INVOKABLE void warning(const QString& category, const QString& message) const;
    Q_INVOKABLE void error(const QString& category, const QString& message) const;
};

} // namespace gittide::ui
