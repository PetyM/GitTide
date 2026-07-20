#include "gittide/ui/avatarimageprovider.hpp"

#include <QImage>
#include <QMetaObject>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <qcorotask.h>

#include "gittide/ui/avatarservice.hpp"

namespace gittide::ui {

namespace {
// One in-flight avatar load. Holds the decoded image until QML asks for it.
class AvatarImageResponse : public QQuickImageResponse
{
public:
    // Called on the AvatarService (main) thread with the resolved image. Marshals
    // onto the response's own thread so m_image, finished(), and textureFactory()
    // are all touched on one thread. The engine keeps the response alive until
    // finished(), so the raw pointer stays valid as long as we always finish.
    void deliver(const QImage& img)
    {
        QMetaObject::invokeMethod(
            this,
            [this, img]
            {
                m_image = img;
                emit finished();
            },
            Qt::QueuedConnection);
    }

    QQuickTextureFactory* textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QImage m_image; // null → transparent; QML initials layer shows through
};
} // namespace

QQuickImageResponse* AvatarImageProvider::requestImageResponse(const QString& id, const QSize&)
{
    auto* response = new AvatarImageResponse;

    // requestImageResponse runs on the QML pixmap-reader thread, but AvatarService
    // and its QNetworkAccessManager live on the main thread. Hop to the service's
    // thread to run the fetch (AutoConnection → queued across threads), keeping the
    // coroutine tied to the service's lifetime.
    AvatarService* service = m_service;
    QMetaObject::invokeMethod(service,
                              [service, id, response]
                              {
                                  QCoro::connect(service->avatarForHash(id), service,
                                                 [response](QImage img) { response->deliver(img); });
                              });
    return response;
}

} // namespace gittide::ui
