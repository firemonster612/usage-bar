#pragma once

#include "providers.h"
#include "usage.h"

#include <QAbstractButton>
#include <QColor>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPixmap>
#include <QWidget>

class QLabel;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

// How a ProviderUsage should be rendered. Owned by main.cpp, which reads it
// from QSettings; the views only apply it.
struct DisplayOptions {
    bool showRemaining = false;
    bool showCost = true;
    bool showCostComparisons = true;
    bool showResetWhenExhausted = true;
};

class UsageBarWidget final : public QWidget {
    Q_OBJECT
public:
    explicit UsageBarWidget(QWidget *parent = nullptr);

    void setValue(int value);
    void setLabel(const QString &label);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateAccessibility();

    int value_;
    QString label_;
};

// Daily spend as a compact bar chart. Hides itself when there is no history to
// draw, so callers can add it unconditionally.
class CostSparkline final : public QWidget {
    Q_OBJECT
public:
    explicit CostSparkline(QWidget *parent = nullptr);

    void setDays(const QList<CostDay> &days);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    int dayAt(int x) const;

    QList<CostDay> days_;
    double max_;
};

// Muted text that reads as a link and clicks like a button. Used for the header's
// "Updated …" line, which doubles as the refresh affordance.
class LinkButton final : public QAbstractButton {
    Q_OBJECT
public:
    explicit LinkButton(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool hovered_ = false;
};

// One segment of the provider switcher: icon above label on a rounded plate that
// fills with the accent colour when selected, plus the weekly-quota hairline that
// only an unselected segment shows.
class ProviderSegmentButton final : public QAbstractButton {
    Q_OBJECT
public:
    ProviderSegmentButton(const ProviderInfo &info, QWidget *parent = nullptr);

    // -1 means the provider reports no weekly window, which hides the indicator
    // rather than drawing an empty track that reads as "nothing left".
    void setRemainingPercent(int percent);

    QSize sizeHint() const override;

signals:
    void navigated(int delta);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateIcons();
    void updateAccessibility();

    ProviderInfo info_;
    int remaining_ = -1;
    bool hovered_ = false;
    QPixmap selectedIcon_;
    QPixmap unselectedIcon_;
};

// One equal-width segment per catalog provider, exactly one of them current.
// Deliberately not a QTabBar: it has to carry the quota hairline and the accent
// plate.
class ProviderSwitcher final : public QWidget {
    Q_OBJECT
public:
    explicit ProviderSwitcher(QWidget *parent = nullptr);

    int currentIndex() const { return current_; }
    void setCurrentIndex(int index);
    void setRemainingPercent(int index, int percent);
    void focusCurrent();

signals:
    void currentChanged(int index);

private:
    QList<ProviderSegmentButton *> segments_;
    int current_ = 0;
};

// A flat menu row: icon, label, optional muted detail lines and a disclosure
// chevron. Restrained hover plate rather than a button frame.
class MenuActionRow final : public QAbstractButton {
    Q_OBJECT
public:
    MenuActionRow(const QString &themeIcon, const QString &glyph, const QString &text,
                  QWidget *parent = nullptr);

    void setDetailLines(const QStringList &lines);
    void setChevron(bool visible);
    void setExpanded(bool expanded);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateAccessibility();

    QString glyph_;
    QStringList details_;
    bool chevron_ = false;
    bool expanded_ = false;
    bool hovered_ = false;
};

// The whole card for exactly one provider: header, usage windows, reset credits,
// cost and the provider-scoped action rows.
class ProviderView final : public QWidget {
    Q_OBJECT
public:
    explicit ProviderView(const ProviderInfo &info, QWidget *parent = nullptr);

    void setUsage(const ProviderUsage *usage, const DisplayOptions &options);
    void setBusy(bool busy);

signals:
    void refreshRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QLabel *addMuted(const QString &text, QWidget *parent);
    void addMetric(const UsageMetric &metric, const DisplayOptions &options);
    void addCost(const UsageCost &cost, bool showComparisons);
    void addSection(const QString &title);
    void addDetailRow(const QString &label, const QString &value);
    void addDivider();
    void addActions();
    void applyMutedColor();
    void elideAccount();

    ProviderInfo info_;
    QString statusUrl_;
    QString statusText_;
    QString metaText_;
    QString accountText_;
    // Survives the rebuild that every refresh triggers, so an expanded Cost
    // section does not snap shut under the user every few minutes.
    bool costExpanded_ = false;
    QLabel *name_;
    QLabel *account_;
    LinkButton *meta_;
    QVBoxLayout *body_;
    QList<QLabel *> muted_;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // True when the window behaves as a tray popover. Decided once at
    // construction: without a tray there is no way to re-open a popover, so the
    // app falls back to a normal, quittable window.
    bool isPopover() const { return popover_; }

public slots:
    // iconGeometry is the tray icon rect and may be invalid or empty: Plasma
    // Wayland (SNI) never reports one. Callers pass QSystemTrayIcon::geometry()
    // as-is and showAt() falls back to the cursor position.
    void showAt(const QRect &iconGeometry = QRect());
    void toggle(const QRect &iconGeometry = QRect());

    void setUsage(const QList<ProviderUsage> &providers, const QString &message);
    void setBusy(bool busy);
    void setDisplayOptions(const DisplayOptions &options);
    void setRefreshOnOpen(bool enabled);

signals:
    void refreshRequested();
    void settingsRequested();
    void aboutRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    void render();
    void selectProvider(int index);
    void positionAt();
    QSize popoverSize() const;

    bool popover_;
    bool refreshOnOpen_ = true;
    // Resolved once per open by showAt(). Kept so a later re-anchor reuses it
    // rather than re-reading the cursor, which by then sits over the popover.
    QRect anchor_;
    // Started every time the popover hides, so toggle() can tell a real
    // "open me" activation from the tray click that just dismissed it.
    QElapsedTimer hiddenAt_;
    QList<ProviderUsage> providers_;
    DisplayOptions options_;
    ProviderSwitcher *switcher_;
    QStackedWidget *stack_;
    QList<ProviderView *> views_;
};
