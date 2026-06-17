#include <QObject>
#include <QtTest/QtTest>

class TestUiSmoke : public QObject
{
    Q_OBJECT
private slots:
    void qt_is_alive()
    {
        QVERIFY(true);
    }
};

#include "test_smoke.moc"
