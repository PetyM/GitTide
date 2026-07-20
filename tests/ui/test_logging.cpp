#include <QtTest>

#include <QDir>
#include <QLoggingCategory>
#include <QString>
#include <QTemporaryDir>

#include <vector>

#include "gittide/log.hpp"
#include "gittide/ui/logging.hpp"

using namespace gittide;
using namespace gittide::ui;

namespace {

// Captures the formatted records the installed message handler produces.
std::vector<QString>* g_captured = nullptr;

void capturingHandler(QtMsgType, const QMessageLogContext& context, const QString& message)
{
    if (g_captured)
        g_captured->push_back(QString::fromUtf8(context.category ? context.category : "") + QStringLiteral("|") + message);
}

} // namespace

class TestLogging : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        // Each test restores the default state so handlers/rules don't leak.
        qInstallMessageHandler(nullptr);
        QLoggingCategory::setFilterRules(QString());
        setLogBackend({});
        g_captured = nullptr;
    }

    void category_for_maps_short_and_canonical_names()
    {
        QCOMPARE(categoryFor("git").categoryName(), "gittide.git");
        QCOMPARE(categoryFor("gittide.git").categoryName(), "gittide.git");
        QCOMPARE(categoryFor("auth").categoryName(), "gittide.auth");
        // Unknown names fall back to the ui category.
        QCOMPARE(categoryFor("nonsense").categoryName(), "gittide.ui");
    }

    void core_bridge_honours_qt_filter_rules()
    {
        installCoreLogBridge();
        QLoggingCategory::setFilterRules(QStringLiteral("gittide.git.debug=true\ngittide.repo.debug=false"));

        QVERIFY(logEnabled(LogLevel::Debug, logcat::GIT));
        QVERIFY(!logEnabled(LogLevel::Debug, logcat::REPO));
        // Warnings stay on by default even where debug is off.
        QVERIFY(logEnabled(LogLevel::Warning, logcat::REPO));
    }

    void core_logf_routes_through_the_bridge_to_the_handler()
    {
        std::vector<QString> captured;
        g_captured = &captured;
        installCoreLogBridge();
        qInstallMessageHandler(capturingHandler);
        QLoggingCategory::setFilterRules(QStringLiteral("gittide.git.debug=true"));

        logf(LogLevel::Debug, logcat::GIT, "fetch {} objects", 7);

        QCOMPARE(captured.size(), size_t(1));
        QCOMPARE(captured[0], QStringLiteral("gittide.git|fetch 7 objects"));
    }

    void qml_log_flows_through_the_same_path()
    {
        std::vector<QString> captured;
        g_captured = &captured;
        installCoreLogBridge();
        qInstallMessageHandler(capturingHandler);
        QLoggingCategory::setFilterRules(QStringLiteral("gittide.ui.debug=true"));

        const QmlLog log;
        log.warning(QStringLiteral("ui"), QStringLiteral("from qml"));
        log.debug(QStringLiteral("ui"), QStringLiteral("debug from qml"));

        QCOMPARE(captured.size(), size_t(2));
        QCOMPARE(captured[0], QStringLiteral("gittide.ui|from qml"));
        QCOMPARE(captured[1], QStringLiteral("gittide.ui|debug from qml"));
    }

    void file_sink_writes_and_rotates_past_the_cap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Tiny cap so a couple of lines force a rotation.
        installMessageHandler(dir.path(), 64);
        QLoggingCategory::setFilterRules(QStringLiteral("gittide.git.debug=true"));
        installCoreLogBridge();

        for (int i = 0; i < 20; ++i)
            logf(LogLevel::Warning, logcat::GIT, "line number {} with some padding text", i);

        const QString logFile = QDir(dir.path()).filePath(QStringLiteral("gittide.log"));
        const QString backup  = logFile + QStringLiteral(".1");
        QVERIFY(QFile::exists(logFile));
        QVERIFY2(QFile::exists(backup), "rotation should have produced a .1 backup");

        // Tear the file sink down *here*, before the QTemporaryDir is removed at end
        // of scope: otherwise the sink keeps gittide.log open, which blocks the
        // directory's removal on Windows and leaves a live sink pointing at a
        // deleted path into process teardown.
        setLogBackend({});
        qInstallMessageHandler(nullptr);
    }
};

#include "test_logging.moc"
