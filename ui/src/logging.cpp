#include "gittide/ui/logging.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

#include "gittide/log.hpp"

namespace gittide::ui {

Q_LOGGING_CATEGORY(catGit, "gittide.git")
Q_LOGGING_CATEGORY(catRepo, "gittide.repo")
Q_LOGGING_CATEGORY(catAsync, "gittide.async")
Q_LOGGING_CATEGORY(catAuth, "gittide.auth")
Q_LOGGING_CATEGORY(catUi, "gittide.ui")
Q_LOGGING_CATEGORY(catApp, "gittide.app")

const QLoggingCategory& categoryFor(std::string_view name)
{
    std::string_view shortName = name;
    if (shortName.starts_with("gittide."))
        shortName.remove_prefix(std::string_view("gittide.").size());

    if (shortName == "git")
        return catGit();
    if (shortName == "repo")
        return catRepo();
    if (shortName == "async")
        return catAsync();
    if (shortName == "auth")
        return catAuth();
    if (shortName == "app")
        return catApp();
    return catUi();
}

namespace {

QString levelLabel(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARNING");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO");
}

QString formatRecord(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString stamp    = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
    const QString category = QString::fromUtf8(context.category ? context.category : "default");
    return QStringLiteral("[%1] %2 %3: %4").arg(stamp, levelLabel(type), category, message);
}

// Appends formatted records to a single file, rolling to a ".1" backup once the
// active file grows past a size cap. Guarded by a mutex because the Qt message
// handler is called from every thread.
class FileSink
{
public:
    void configure(const QString& dir, qint64 maxBytes)
    {
        const QMutexLocker locker(&m_mutex);
        m_maxBytes = maxBytes;
        m_file.close();
        if (dir.isEmpty())
        {
            m_path.clear();
            return;
        }
        QDir().mkpath(dir);
        m_path = QDir(dir).filePath(QStringLiteral("gittide.log"));
        openAppend();
    }

    void write(const QString& line)
    {
        const QMutexLocker locker(&m_mutex);
        if (!m_file.isOpen())
            return;
        rotateIfNeeded();
        QTextStream stream(&m_file);
        stream << line << '\n';
        stream.flush();
    }

private:
    // Opens m_path for appending; on failure m_path is left set but the file stays
    // closed, so write() simply drops records (logging never aborts the app).
    void openAppend()
    {
        m_file.setFileName(m_path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            m_file.close();
    }

    void rotateIfNeeded()
    {
        if (m_maxBytes <= 0 || m_file.size() < m_maxBytes)
            return;
        m_file.close();
        const QString backup = m_path + QStringLiteral(".1");
        QFile::remove(backup);
        QFile::rename(m_path, backup);
        openAppend();
    }

    QMutex  m_mutex;
    QFile   m_file;
    QString m_path;
    qint64  m_maxBytes = 0;
};

FileSink& fileSink()
{
    static FileSink sink;
    return sink;
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString record = formatRecord(type, context, message);
    std::fputs(record.toLocal8Bit().constData(), stderr);
    std::fputc('\n', stderr);
    fileSink().write(record);
}

void levelToLogger(LogLevel level, const QLoggingCategory& category, const QString& message)
{
    switch (level)
    {
    case LogLevel::Trace:
    case LogLevel::Debug:
        QMessageLogger().debug(category).noquote() << message;
        break;
    case LogLevel::Info:
        QMessageLogger().info(category).noquote() << message;
        break;
    case LogLevel::Warning:
        QMessageLogger().warning(category).noquote() << message;
        break;
    case LogLevel::Error:
        QMessageLogger().critical(category).noquote() << message;
        break;
    }
}

bool categoryEnabled(LogLevel level, const QLoggingCategory& category)
{
    switch (level)
    {
    case LogLevel::Trace:
    case LogLevel::Debug:
        return category.isDebugEnabled();
    case LogLevel::Info:
        return category.isInfoEnabled();
    case LogLevel::Warning:
        return category.isWarningEnabled();
    case LogLevel::Error:
        return category.isCriticalEnabled();
    }
    return false;
}

} // namespace

void installCoreLogBridge()
{
    gittide::setLogBackend(gittide::LogBackend{
        .write =
            [](LogLevel level, std::string_view category, std::string_view message)
        {
            const QLoggingCategory& cat = categoryFor(category);
            levelToLogger(level, cat, QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size())));
        },
        .enabled = [](LogLevel level, std::string_view category) { return categoryEnabled(level, categoryFor(category)); },
    });
}

QtMessageHandler installMessageHandler(const QString& logDir, qint64 maxBytes)
{
    fileSink().configure(logDir, maxBytes);
    return qInstallMessageHandler(messageHandler);
}

void installLogging(const QString& logDir)
{
    installCoreLogBridge();
    installMessageHandler(logDir);
}

QmlLog::QmlLog(QObject* parent)
    : QObject(parent)
{
}

namespace {

void logFromQml(LogLevel level, const QString& category, const QString& message)
{
    const std::string cat = category.toStdString();
    if (gittide::logEnabled(level, cat))
        gittide::logMessage(level, cat, message.toStdString());
}

} // namespace

void QmlLog::trace(const QString& category, const QString& message) const
{
    logFromQml(LogLevel::Trace, category, message);
}

void QmlLog::debug(const QString& category, const QString& message) const
{
    logFromQml(LogLevel::Debug, category, message);
}

void QmlLog::info(const QString& category, const QString& message) const
{
    logFromQml(LogLevel::Info, category, message);
}

void QmlLog::warning(const QString& category, const QString& message) const
{
    logFromQml(LogLevel::Warning, category, message);
}

void QmlLog::error(const QString& category, const QString& message) const
{
    logFromQml(LogLevel::Error, category, message);
}

} // namespace gittide::ui
