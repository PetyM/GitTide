#pragma once
#include <QByteArray>
#include <QString>
#include <vector>

namespace gitgui::ui {

struct WindowSession {
    QString projectId;
    QByteArray geometry;     // QMainWindow::saveGeometry() bytes
    QString lastActiveRepo;
};

// Multi-window session state, persisted separately from the project registry
// (spec §5). Corrupt/missing input never throws — yields an empty store.
class SessionStore {
public:
    static constexpr int kVersion = 1;

    std::vector<WindowSession> windows;

    QByteArray toJson() const;
    static SessionStore fromJson(const QByteArray& json);

    // Atomic save (temp file + rename). Returns false on I/O failure.
    bool save(const QString& file) const;
    // Missing/corrupt file -> empty store.
    static SessionStore load(const QString& file);
};

}  // namespace gitgui::ui
