#pragma once
#include <QQuickAsyncImageProvider>

namespace gittide::ui {

class AvatarService;

/// QML async image provider bridging `AvatarService` to `Image { source:
/// "image://avatar/<md5-email-hash>" }`. The request id is the md5 hash the QML
/// side builds from a row's `authorEmail` (via `AvatarService::emailHash`), so
/// the engine's per-source pixmap cache dedups identical authors across the
/// virtualized History rows. A null/failed result yields a transparent image, so
/// the QML `Avatar` initials layer shows through.
class AvatarImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit AvatarImageProvider(AvatarService* service) : m_service(service) {}

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    AvatarService* m_service;
};

} // namespace gittide::ui
