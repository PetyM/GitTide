#pragma once
#include <QObject>
#include <QString>

#include "gittide/ui/systemcolorscheme.hpp"
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

    /// Resolves System mode against a PortalColorScheme it owns.
    explicit ThemeManager(QObject* parent = nullptr);
    /// Resolves System mode against an injected source (borrowed — it must
    /// outlive the manager). For tests and for callers with their own source.
    explicit ThemeManager(SystemColorScheme* source, QObject* parent = nullptr);

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
    bool resolveDark() const; // System → m_source->colorScheme(); else forced
    Mode m_mode = Mode::System;
    SystemColorScheme* m_source; // borrowed; owned only in the default ctor
};

} // namespace gittide::ui
