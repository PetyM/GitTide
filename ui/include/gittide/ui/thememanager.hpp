#pragma once
#include <QObject>
#include <QString>
#include "gittide/ui/theme.hpp"

class QApplication;

namespace gittide::ui {

// Owns the active theme mode, resolves System mode against the OS color scheme,
// applies the generated stylesheet app-wide, and exposes the matching icon.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Dark, Light };

    explicit ThemeManager(QObject* parent = nullptr);

    void  setMode(Mode mode);
    Mode  mode() const { return mode_; }
    Theme currentTheme() const;
    QString iconResource() const;
    void  applyTo(QApplication* app);

signals:
    void themeChanged();

private:
    bool resolveDark() const;   // System → QStyleHints::colorScheme; else forced
    Mode mode_ = Mode::System;
    QApplication* app_ = nullptr;
};

}  // namespace gittide::ui
