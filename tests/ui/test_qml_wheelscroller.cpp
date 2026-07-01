#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>

class TestQmlWheelScroller : public QObject
{
    Q_OBJECT
private slots:

    // WheelScroller is always declared as a direct child of a ListView/Flickable.
    // Flickable's default property redirects declared children into its internal
    // contentItem, so WheelHandler.parent is that contentItem, not the Flickable —
    // `view` must skip past it via parent.parent, not bind to `parent` directly.
    void view_resolves_to_the_enclosing_flickable_not_its_content_item()
    {
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData(R"QML(
            import QtQuick
            ListView {
                id: lv
                objectName: "lv"
                width: 200
                height: 200
                model: 20
                delegate: Item { height: 20 }
                WheelScroller { id: scroller; objectName: "scroller" }
            }
        )QML", QUrl(QStringLiteral("qrc:/qml/_test_wheelscroller_host.qml")));

        QObject* root = comp.create();
        QVERIFY2(root, qPrintable(comp.errorString()));

        QObject* scroller = root->findChild<QObject*>(QStringLiteral("scroller"));
        QVERIFY(scroller != nullptr);

        // `view` must be the ListView itself (root), not its contentItem.
        const QVariant viewVar = scroller->property("view");
        QCOMPARE(viewVar.value<QObject*>(), root);

        delete root;
    }
};

#include "test_qml_wheelscroller.moc"
