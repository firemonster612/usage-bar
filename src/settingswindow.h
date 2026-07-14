#pragma once

#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QSettings;
class QSpinBox;
class QTabWidget;

// Validated, ascending notification thresholds. Lives here rather than in
// main.cpp so the editor below and the notification tracker cannot disagree
// about what a stored value means.
QList<int> usageNotificationThresholds(QSettings &settings);

// A non-modal, native settings window. Every control writes straight to
// QSettings and emits changed(), so there is nothing to save and no prompt to
// dismiss.
class SettingsWindow final : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWindow(QSettings *settings, QWidget *parent = nullptr);

    // Lets the popover's About row land on the page it names instead of whatever
    // tab the user last left open.
    void showAboutPage();

signals:
    void changed();

private:
    QWidget *buildGeneral();
    QWidget *buildDisplay();
    QWidget *buildNotifications();
    QWidget *buildAbout();
    void writeThresholds();

    QSettings *settings_;
    QTabWidget *tabs_;
    QCheckBox *autostart_;
    QComboBox *interval_;
    QCheckBox *refreshOnOpen_;
    QCheckBox *showRemaining_;
    QCheckBox *showCost_;
    QCheckBox *showCostComparisons_;
    QCheckBox *showResetWhenExhausted_;
    QCheckBox *notify_;
    QList<QSpinBox *> thresholds_;
};
