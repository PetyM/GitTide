#pragma once
#include <QObject>
#include <QString>

#include "gittide/ui/theme.hpp"

namespace gittide::ui {

// Owns the active theme mode, resolves System mode against the OS color scheme,
// and exposes the active token table plus the matching app icon. Pure model: the
// QML layer (QmlTheme) reads currentTheme() and reacts to themeChanged() — there
// is no QWidgets QPalette/stylesheet path anymore.
class ThemeManager : public QObject
{
    Q_OBJECT
public:
    enum class Mode
    {
        System,
        Dark,
        Light
    };

    explicit ThemeManager(QObject* parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const
    {
        return m_mode;
    }
    Theme currentTheme() const;
    QString iconResource() const;

signals:
    void themeChanged();

private:
    bool resolveDark() const; // System → QStyleHints::colorScheme; else forced
    Mode m_mode = Mode::System;
};

} // namespace gittide::ui
