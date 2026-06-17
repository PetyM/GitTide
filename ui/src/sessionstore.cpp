#include "gittide/ui/sessionstore.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace gittide::ui {

QByteArray SessionStore::toJson() const
{
    QJsonArray arr;
    for (const auto& w : windows)
    {
        QJsonObject o;
        o["projectId"]      = w.projectId;
        o["geometry"]       = QString::fromLatin1(w.geometry.toBase64());
        o["lastActiveRepo"] = w.lastActiveRepo;
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = kVersion;
    root["windows"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

SessionStore SessionStore::fromJson(const QByteArray& json)
{
    SessionStore s;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return s; // empty
    const QJsonArray arr = doc.object().value("windows").toArray();
    for (const auto& v : arr)
    {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        WindowSession w;
        w.projectId      = o.value("projectId").toString();
        w.geometry       = QByteArray::fromBase64(o.value("geometry").toString().toLatin1());
        w.lastActiveRepo = o.value("lastActiveRepo").toString();
        s.windows.push_back(std::move(w));
    }
    return s;
}

bool SessionStore::save(const QString& file) const
{
    QSaveFile f(file); // writes to temp, atomic commit on success
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(toJson()) < 0)
    {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

SessionStore SessionStore::load(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return SessionStore{}; // missing -> empty
    return fromJson(f.readAll());
}

} // namespace gittide::ui
