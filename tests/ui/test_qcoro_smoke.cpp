#include <QObject>
#include <QtConcurrent>
#include <QtTest/QtTest>
#include <core/qcorofuture.h>
#include <qcorotask.h>

namespace {
QCoro::Task<int> doubled_on_pool(int n)
{
    co_return co_await QtConcurrent::run(
        [n]()
        {
            return n * 2;
        });
}
} // namespace

class TestQCoroSmoke : public QObject
{
    Q_OBJECT
private slots:
    void awaits_a_pool_task()
    {
        const int result = QCoro::waitFor(doubled_on_pool(21));
        QCOMPARE(result, 42);
    }
};

#include "test_qcoro_smoke.moc"
