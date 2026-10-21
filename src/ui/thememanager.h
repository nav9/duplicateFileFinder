#pragma once

#include "core/settings.h"

#include <QObject>
#include <QString>

class QApplication;

namespace dedupvault::ui {

/**
 * @brief Applies the application theme (dark by default, light optional).
 *
 * The stylesheets are plain Qt stylesheets so the palette stays
 * maintainable; Settings switches themes at runtime without a restart.
 */
class ThemeManager : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject* parent = nullptr);

    /// Apply a theme to the running application.
    void apply(core::AppSettings::Theme theme);
    core::AppSettings::Theme current() const { return m_theme; }

    static QString darkStylesheet();
    static QString lightStylesheet();

private:
    core::AppSettings::Theme m_theme = core::AppSettings::Theme::Dark;
};

} // namespace dedupvault::ui