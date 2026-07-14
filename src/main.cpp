#include "mainwindow.h"
#include "notificationtracker.h"
#include "settingswindow.h"
#include "singleinstance.h"
#include "usagebackend.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QIcon>
#include <QMenu>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>

#include <unistd.h>

#ifndef USAGEBAR_VERSION
#define USAGEBAR_VERSION "0.0.0"
#endif

static bool sendDesktopNotification(const QString &title, const QString &body)
{
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()
        || !bus.interface()->isServiceRegistered("org.freedesktop.Notifications").value())
        return false;
    QDBusInterface notifications("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                 "org.freedesktop.Notifications", bus);
    if (!notifications.isValid())
        return false;
    const auto call = notifications.asyncCall("Notify", QStringLiteral("UsageBar"), uint(0),
                                               QStringLiteral("usagebar"), title, body,
                                               QStringList{}, QVariantMap{}, 8000);
    return !call.isError();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("UsageBar"));
    QApplication::setOrganizationName(QStringLiteral("UsageBar"));
    QApplication::setApplicationVersion(QStringLiteral(USAGEBAR_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    MainWindow window;
    const bool requestShow = app.arguments().contains(QStringLiteral("--show"));
    SingleInstance instance(QStringLiteral("io.github.usagebar.UsageBar-%1").arg(getuid()));
    auto showWindow = [&window] { window.showAt(); };
    QObject::connect(&instance, &SingleInstance::showRequested, &window, showWindow);
    if (!instance.start(requestShow))
        return 0;

    UsageBackend backend;
    QSettings settings;
    NotificationTracker notifications;
    QObject::connect(&window, &MainWindow::refreshRequested, &backend, &UsageBackend::refresh);
    QObject::connect(&backend, &UsageBackend::updated, &window, &MainWindow::setUsage);
    QObject::connect(&backend, &UsageBackend::busyChanged, &window, &MainWindow::setBusy);

    QSystemTrayIcon tray(QIcon::fromTheme("usagebar-symbolic",
                                          QIcon(QStringLiteral(":/assets/usagebar-symbolic.svg"))));
    QMenu menu;
    auto *open = menu.addAction(QStringLiteral("Open Usage"));
    auto *refresh = menu.addAction(QStringLiteral("Refresh"));
    // Start at login lives in Settings ▸ General; duplicating it here would give
    // the same preference two places to disagree.
    auto *settingsAction = menu.addAction(QStringLiteral("Settings…"));
    menu.addSeparator();
    auto *quit = menu.addAction(QStringLiteral("Quit"));
    tray.setContextMenu(&menu);
    tray.setToolTip(QStringLiteral("UsageBar"));

    // "Open Usage" always shows; only the icon itself toggles.
    QObject::connect(open, &QAction::triggered, &window, [&window, &tray] { window.showAt(tray.geometry()); });
    QObject::connect(refresh, &QAction::triggered, &backend, &UsageBackend::refresh);
    QObject::connect(quit, &QAction::triggered, &app, &QApplication::quit);

    SettingsWindow settingsWindow(&settings);
    auto showSettings = [&settingsWindow] {
        settingsWindow.show();
        settingsWindow.raise();
        settingsWindow.activateWindow();
    };
    QObject::connect(settingsAction, &QAction::triggered, &settingsWindow, showSettings);
    QObject::connect(&window, &MainWindow::settingsRequested, &settingsWindow, showSettings);
    QObject::connect(&window, &MainWindow::aboutRequested, &settingsWindow,
                     &SettingsWindow::showAboutPage);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &window,
                     [&window, &tray](QSystemTrayIcon::ActivationReason reason) {
        // geometry() is empty on Plasma Wayland; toggle() falls back to the cursor.
        if (reason == QSystemTrayIcon::Trigger)
            window.toggle(tray.geometry());
    });
    QObject::connect(&backend, &UsageBackend::busyChanged, refresh, &QAction::setDisabled);
    QObject::connect(&backend, &UsageBackend::updated, [&tray](const QList<ProviderUsage> &providers, const QString &) {
        QStringList lines{QStringLiteral("UsageBar")};
        for (const auto &provider : providers) {
            if (!provider.metrics.isEmpty())
                lines.append(QStringLiteral("%1: %2% used").arg(provider.name).arg(provider.metrics.first().usedPercent));
        }
        tray.setToolTip(lines.join('\n'));
    });
    QObject::connect(&backend, &UsageBackend::updated,
                     [&tray, &settings, &notifications](const QList<ProviderUsage> &providers, const QString &) {
        // Track thresholds even while notifications are off, so re-enabling them
        // does not replay every crossing that happened in the meantime.
        const auto alerts = notifications.update(providers, usageNotificationThresholds(settings));
        if (!settings.value("notifications/enabled", true).toBool())
            return;
        for (const auto &alert : alerts) {
            const auto title = QStringLiteral("%1 usage").arg(alert.provider);
            const auto body = QStringLiteral("%1 reached %2% used (%3% now)")
                                  .arg(alert.window).arg(alert.threshold).arg(alert.usedPercent);
            if (!sendDesktopNotification(title, body))
                tray.showMessage(title, body, QSystemTrayIcon::Information, 8000);
        }
    });

    QTimer refreshTimer;
    auto applySettings = [&settings, &window, &refreshTimer] {
        window.setRefreshOnOpen(settings.value("general/refreshOnOpen", true).toBool());
        window.setDisplayOptions({settings.value("display/showRemaining", false).toBool(),
                                  settings.value("display/showCost", true).toBool(),
                                  settings.value("display/showCostComparisons", true).toBool(),
                                  settings.value("display/showResetWhenExhausted", true).toBool()});
        const auto minutes = settings.value("general/refreshIntervalMinutes", 5).toInt();
        if (minutes <= 0) {
            refreshTimer.stop();
            return;
        }
        refreshTimer.setInterval(minutes * 60 * 1000);
        refreshTimer.start();
    };
    QObject::connect(&refreshTimer, &QTimer::timeout, &backend, &UsageBackend::refresh);
    QObject::connect(&settingsWindow, &SettingsWindow::changed, &app, applySettings);
    applySettings();

    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray.show();
    if (requestShow)
        showWindow();

    QTimer::singleShot(0, &backend, &UsageBackend::refresh);
    return app.exec();
}
