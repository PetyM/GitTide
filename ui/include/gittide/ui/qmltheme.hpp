#pragma once
#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>

#include "gittide/ui/thememanager.hpp"

namespace gittide::ui {

// Exposes the active Theme's tokens to QML as bindable properties. Wraps a
// ThemeManager and re-emits changed() when the manager's theme changes, so every
// QML binding refreshes on a live theme switch. Colours are QColor (QML-native).
// laneColors is the graph lane palette — the one place GitTide uses >1 hue — and
// is CONSTANT because it does not vary by theme.
class QmlTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QColor surfaceBase READ surfaceBase NOTIFY changed)
    Q_PROPERTY(QColor surfaceRaised READ surfaceRaised NOTIFY changed)
    Q_PROPERTY(QColor surfaceOverlay READ surfaceOverlay NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY changed)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY changed)
    Q_PROPERTY(QColor head READ head NOTIFY changed)
    Q_PROPERTY(QColor stateAdded READ stateAdded NOTIFY changed)
    Q_PROPERTY(QColor stateModified READ stateModified NOTIFY changed)
    Q_PROPERTY(QColor stateDeleted READ stateDeleted NOTIFY changed)
    Q_PROPERTY(QColor stateUntracked READ stateUntracked NOTIFY changed)
    Q_PROPERTY(QColor stateConflict READ stateConflict NOTIFY changed)
    Q_PROPERTY(QColor shadow READ shadow NOTIFY changed)
    Q_PROPERTY(QVariantList laneColors READ laneColors CONSTANT)
    Q_PROPERTY(QString iconSource READ iconSource NOTIFY changed)

public:
    explicit QmlTheme(ThemeManager* manager, QObject* parent = nullptr);

    bool dark() const;
    QColor surfaceBase() const;
    QColor surfaceRaised() const;
    QColor surfaceOverlay() const;
    QColor border() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor textMuted() const;
    QColor accent() const;
    QColor accentHover() const;
    QColor head() const;
    QColor stateAdded() const;
    QColor stateModified() const;
    QColor stateDeleted() const;
    QColor stateUntracked() const;
    QColor stateConflict() const;
    QColor shadow() const;
    QVariantList laneColors() const;
    QString iconSource() const;

signals:
    void changed();

private:
    Theme theme() const;
    ThemeManager* m_manager;
};

} // namespace gittide::ui
