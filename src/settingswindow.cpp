#include "settingswindow.h"

#include "usagebackend.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

// 0 means "never poll"; the user refreshes by hand or by opening the window.
static constexpr int kIntervals[] = {0, 1, 2, 5, 15};
static constexpr int kDefaultInterval = 5;

static QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + "/autostart/io.github.usagebar.UsageBar.desktop";
}

static bool setAutostart(bool enabled)
{
    const auto path = autostartPath();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    // Inside an AppImage the running binary is a temporary mount point, so the
    // launcher has to point back at the .AppImage itself to survive a reboot.
    auto executable = qEnvironmentVariable("APPIMAGE");
    if (executable.isEmpty())
        executable = QCoreApplication::applicationFilePath();
    file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=UsageBar\nExec=\"%1\" --background\nIcon=usagebar\nTerminal=false\nX-GNOME-Autostart-enabled=true\n")
                   .arg(executable.replace('"', "\\\"")).toUtf8());
    return true;
}

QList<int> usageNotificationThresholds(QSettings &settings)
{
    QList<int> thresholds;
    const auto stored = settings.value("notifications/thresholds", QVariantList{75, 90, 100}).toList();
    for (const auto &value : stored) {
        const auto threshold = value.toInt();
        if (threshold >= 1 && threshold <= 100 && !thresholds.contains(threshold))
            thresholds.append(threshold);
    }
    std::sort(thresholds.begin(), thresholds.end());
    if (thresholds.size() != 3 || thresholds[0] >= thresholds[1] || thresholds[1] >= thresholds[2])
        return {75, 90, 100};
    return thresholds;
}

SettingsWindow::SettingsWindow(QSettings *settings, QWidget *parent)
    : QWidget(parent, Qt::Window), settings_(settings)
{
    setWindowTitle(QStringLiteral("UsageBar Settings"));
    setWindowIcon(QIcon::fromTheme("usagebar", QIcon(QStringLiteral(":/assets/usagebar-256.png"))));

    tabs_ = new QTabWidget(this);
    tabs_->addTab(buildGeneral(), QStringLiteral("General"));
    tabs_->addTab(buildDisplay(), QStringLiteral("Display"));
    tabs_->addTab(buildNotifications(), QStringLiteral("Notifications"));
    tabs_->addTab(buildAbout(), QStringLiteral("About"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs_);
}

void SettingsWindow::showAboutPage()
{
    tabs_->setCurrentIndex(tabs_->count() - 1);
    show();
    raise();
    activateWindow();
}

QWidget *SettingsWindow::buildGeneral()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);

    autostart_ = new QCheckBox(QStringLiteral("Start UsageBar at login"), page);
    autostart_->setChecked(QFile::exists(autostartPath()));
    layout->addRow(autostart_);
    connect(autostart_, &QCheckBox::toggled, this, [this](bool enabled) {
        // Writing the launcher can fail on a read-only or full config dir. Snap
        // the checkbox back rather than claiming a setting that will not stick.
        if (!setAutostart(enabled)) {
            QSignalBlocker blocker(autostart_);
            autostart_->setChecked(!enabled);
        }
    });

    interval_ = new QComboBox(page);
    for (const int minutes : kIntervals) {
        const auto text = minutes == 0 ? QStringLiteral("Manual")
            : minutes == 1             ? QStringLiteral("Every minute")
                                       : QStringLiteral("Every %1 minutes").arg(minutes);
        interval_->addItem(text, minutes);
    }
    const auto stored = settings_->value("general/refreshIntervalMinutes", kDefaultInterval).toInt();
    const int index = interval_->findData(stored);
    interval_->setCurrentIndex(index >= 0 ? index : interval_->findData(kDefaultInterval));
    layout->addRow(QStringLiteral("Refresh"), interval_);
    connect(interval_, &QComboBox::currentIndexChanged, this, [this] {
        settings_->setValue("general/refreshIntervalMinutes", interval_->currentData().toInt());
        emit changed();
    });

    refreshOnOpen_ = new QCheckBox(QStringLiteral("Refresh when the window opens"), page);
    refreshOnOpen_->setChecked(settings_->value("general/refreshOnOpen", true).toBool());
    layout->addRow(refreshOnOpen_);
    connect(refreshOnOpen_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_->setValue("general/refreshOnOpen", enabled);
        emit changed();
    });

    return page;
}

QWidget *SettingsWindow::buildDisplay()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);

    showRemaining_ = new QCheckBox(QStringLiteral("Show remaining instead of used"), page);
    showRemaining_->setChecked(settings_->value("display/showRemaining", false).toBool());
    layout->addRow(showRemaining_);
    connect(showRemaining_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_->setValue("display/showRemaining", enabled);
        emit changed();
    });

    showCost_ = new QCheckBox(QStringLiteral("Show the cost section"), page);
    showCost_->setChecked(settings_->value("display/showCost", true).toBool());
    layout->addRow(showCost_);
    connect(showCost_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_->setValue("display/showCost", enabled);
        emit changed();
    });

    showCostComparisons_ = new QCheckBox(QStringLiteral("Show 7 and 90-day cost comparisons"), page);
    showCostComparisons_->setChecked(settings_->value("display/showCostComparisons", true).toBool());
    showCostComparisons_->setEnabled(showCost_->isChecked());
    layout->addRow(showCostComparisons_);
    connect(showCost_, &QCheckBox::toggled, showCostComparisons_, &QWidget::setEnabled);
    connect(showCostComparisons_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_->setValue("display/showCostComparisons", enabled);
        emit changed();
    });

    showResetWhenExhausted_ = new QCheckBox(QStringLiteral("Show reset time when quota is exhausted"), page);
    showResetWhenExhausted_->setChecked(settings_->value("display/showResetWhenExhausted", true).toBool());
    layout->addRow(showResetWhenExhausted_);
    connect(showResetWhenExhausted_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_->setValue("display/showResetWhenExhausted", enabled);
        emit changed();
    });

    return page;
}

QWidget *SettingsWindow::buildNotifications()
{
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);

    notify_ = new QCheckBox(QStringLiteral("Notify when usage crosses a threshold"), page);
    notify_->setChecked(settings_->value("notifications/enabled", true).toBool());
    layout->addRow(notify_);
    connect(notify_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_->setValue("notifications/enabled", enabled);
        emit changed();
    });

    const auto stored = usageNotificationThresholds(*settings_);
    for (int index = 0; index < 3; ++index) {
        auto *spin = new QSpinBox(page);
        spin->setRange(index + 1, 98 + index);
        spin->setSuffix(QStringLiteral("% used"));
        spin->setValue(stored[index]);
        layout->addRow(QStringLiteral("Threshold %1").arg(index + 1), spin);
        thresholds_.append(spin);
    }
    // Keep the three strictly ascending by moving the neighbours' bounds, so the
    // stored list can never fail usageNotificationThresholds()' validation.
    connect(thresholds_[0], &QSpinBox::valueChanged, this, [this](int value) {
        thresholds_[1]->setMinimum(value + 1);
        writeThresholds();
    });
    connect(thresholds_[1], &QSpinBox::valueChanged, this, [this](int value) {
        thresholds_[0]->setMaximum(value - 1);
        thresholds_[2]->setMinimum(value + 1);
        writeThresholds();
    });
    connect(thresholds_[2], &QSpinBox::valueChanged, this, [this](int value) {
        thresholds_[1]->setMaximum(value - 1);
        writeThresholds();
    });

    return page;
}

void SettingsWindow::writeThresholds()
{
    QVariantList stored;
    for (auto *spin : thresholds_)
        stored.append(spin->value());
    settings_->setValue("notifications/thresholds", stored);
    emit changed();
}

QWidget *SettingsWindow::buildAbout()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *name = new QLabel(QStringLiteral("UsageBar %1").arg(QCoreApplication::applicationVersion()), page);
    auto font = name->font();
    font.setBold(true);
    name->setFont(font);
    layout->addWidget(name);

    auto *about = new QLabel(
        QStringLiteral("Codex and Claude usage in the system tray."), page);
    about->setWordWrap(true);
    layout->addWidget(about);

    // The CLI is the only data source, so where it was found (or that it was
    // not) is the first thing worth knowing when the numbers look wrong.
    const auto cli = UsageBackend::findCli();
    auto *backend = new QLabel(cli.isEmpty() ? QStringLiteral("CodexBarCLI: not found")
                                             : QStringLiteral("CodexBarCLI: %1").arg(cli), page);
    backend->setWordWrap(true);
    backend->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(backend);

    auto *credit = new QLabel(
        QStringLiteral("Usage data comes from <a href=\"https://github.com/steipete/codexbar\">CodexBar</a>."),
        page);
    credit->setOpenExternalLinks(true);
    credit->setWordWrap(true);
    layout->addWidget(credit);

    layout->addStretch();
    return page;
}
