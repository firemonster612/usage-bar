#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QSystemTrayIcon>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

static constexpr int kBarHeight = 6;
static constexpr int kBarWidthHint = 160;
static constexpr int kHeaderSpacing = 12;
static constexpr qreal kTrackAlpha = 0.11;

static constexpr int kSparkHeight = 34;
static constexpr int kSparkGap = 1;
static constexpr qreal kSparkAlpha = 0.55;

// The card's left/right content grid. Rows bleed to the full width and inset
// their own contents to it, so their hover plate spans the popover.
static constexpr int kCardPadding = 16;
static constexpr int kRowIconSize = 16;
static constexpr int kRowIconGap = 10;
static constexpr int kRowPadding = 6;
static constexpr int kPlateRadius = 6;
static constexpr qreal kHoverAlpha = 0.08;

static constexpr int kSegmentIconSize = 18;
static constexpr int kSegmentPaddingTop = 7;
static constexpr int kSegmentPaddingBottom = 9;
static constexpr int kSegmentIconGap = 4;
// Six segments share the popover's width, so the plate hugs its label rather
// than carrying the card's generous side padding.
static constexpr int kSegmentPaddingSide = 6;
static constexpr int kSegmentSpacing = 2;
// The labels run to "Copilot" at a sixth of the width; a touch under the body
// font keeps the longest one off the elide threshold.
static constexpr qreal kSegmentFontScale = 0.9;
static constexpr int kQuotaHeight = 2;
static constexpr int kQuotaBottomInset = 3;
static constexpr int kQuotaSideInset = 6;

// Deliberately fixed rather than hugging the content: the card is a tall menu,
// and a short provider should not shrink it into a stub panel.
static constexpr int kPopoverWidth = 420;
static constexpr int kPopoverHeight = 740;
static constexpr int kPanelRadius = 12;
static constexpr qreal kPanelBorderAlpha = 0.15;
// Gap between the tray icon (or cursor) and the popover edge.
static constexpr int kAnchorGap = 8;
// Keeps the popover clear of screen edges and panels.
static constexpr int kScreenMargin = 8;
// A tray activation arriving within this window of an auto-hide is the same
// click that dismissed the popover, not a request to re-open it.
static constexpr qint64 kDismissGuardMs = 250;

static bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

static QColor mutedColor(const QPalette &palette)
{
    const auto placeholder = palette.color(QPalette::PlaceholderText);
    if (placeholder.isValid() && placeholder.alpha() > 0)
        return placeholder;
    return palette.color(QPalette::Disabled, QPalette::Text);
}

static qreal relativeLuminance(const QColor &color)
{
    const auto channel = [](qreal value) {
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF())
        + 0.0722 * channel(color.blueF());
}

static qreal contrastRatio(const QColor &first, const QColor &second)
{
    const auto high = qMax(relativeLuminance(first), relativeLuminance(second));
    const auto low = qMin(relativeLuminance(first), relativeLuminance(second));
    return (high + 0.05) / (low + 0.05);
}

// What to draw on top of a filled accent plate. HighlightedText is the theme's
// own answer and is usually right, but a handful of themes ship one that barely
// contrasts with their accent — a white-on-white icon is worse than ignoring the
// theme, so fall back to whichever of black/white actually reads.
static QColor onAccentColor(const QPalette &palette)
{
    const auto accent = palette.color(QPalette::Highlight);
    if (contrastRatio(Qt::white, accent) >= 3.0)
        return Qt::white;
    const auto themed = palette.color(QPalette::HighlightedText);
    if (themed.isValid() && contrastRatio(themed, accent) >= 3.0)
        return themed;
    return contrastRatio(Qt::white, accent) >= contrastRatio(Qt::black, accent) ? QColor(Qt::white)
                                                                               : QColor(Qt::black);
}

static QColor overlayColor(const QPalette &palette, qreal alpha)
{
    auto color = palette.color(QPalette::Text);
    color.setAlphaF(alpha);
    return color;
}

static QFont scaledFont(const QFont &base, qreal factor)
{
    QFont font = base;
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * factor);
    else if (font.pixelSize() > 0)
        font.setPixelSize(qMax(1, qRound(font.pixelSize() * factor)));
    return font;
}

static void setMutedColor(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(label->foregroundRole(), color);
    label->setPalette(palette);
}

// The upstream artwork is a solid white silhouette, so its alpha is a mask: draw
// it, then flood the shape with the colour the state calls for. That keeps one
// asset readable on both the accent plate and the window background instead of
// needing a light and a dark copy.
static QPixmap tintedIcon(const QString &path, const QColor &color, int size, qreal ratio)
{
    QSvgRenderer renderer(path);
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRectF(0, 0, size, size), color);
    return pixmap;
}

static QFrame *makeDivider(QWidget *parent)
{
    auto *divider = new QFrame(parent);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    divider->setFixedHeight(1);
    return divider;
}

static QString formatCost(double cost)
{
    // The CLI reports USD, so format the symbol rather than letting the locale
    // relabel the same number as the user's home currency.
    return QStringLiteral("$%1").arg(cost, 0, 'f', 2);
}

static QString formatTokens(qint64 tokens)
{
    if (tokens < 1000)
        return QStringLiteral("%1 tokens").arg(tokens);
    if (tokens < 1000000)
        return QStringLiteral("%1K tokens").arg(tokens / 1000.0, 0, 'f', 1);
    if (tokens < 1000000000)
        return QStringLiteral("%1M tokens").arg(tokens / 1000000.0, 0, 'f', 1);
    return QStringLiteral("%1B tokens").arg(tokens / 1000000000.0, 0, 'f', 1);
}

static QString formatStamp(const QString &iso)
{
    const auto stamp = QDateTime::fromString(iso, Qt::ISODate);
    // Pass anything unparseable straight through: showing the raw string beats
    // silently dropping the only freshness cue we have.
    if (!stamp.isValid())
        return iso;
    return QLocale().toString(stamp.toLocalTime(), QLocale::ShortFormat);
}

static QString formatUpdated(const QString &iso)
{
    const auto stamp = QDateTime::fromString(iso, Qt::ISODate);
    if (!stamp.isValid())
        return iso;
    const auto seconds = stamp.toUTC().secsTo(QDateTime::currentDateTimeUtc());
    if (seconds < 0 || seconds < 60)
        return QStringLiteral("just now");
    if (seconds < 3600)
        return QStringLiteral("%1m ago").arg(seconds / 60);
    if (seconds < 86400)
        return QStringLiteral("%1h ago").arg(seconds / 3600);
    return QLocale().toString(stamp.toLocalTime(), QLocale::ShortFormat);
}

static QString resetCountdown(const UsageMetric &metric)
{
    const auto reset = QDateTime::fromString(metric.cycle, Qt::ISODate);
    const auto seconds = QDateTime::currentDateTimeUtc().secsTo(reset.toUTC());
    if (!reset.isValid() || seconds <= 0)
        return metric.reset;
    if (seconds < 3600)
        return QStringLiteral("Resets in %1m").arg(qMax<qint64>(1, seconds / 60));
    if (seconds < 86400)
        return QStringLiteral("Resets in %1h %2m").arg(seconds / 3600).arg(seconds % 3600 / 60);
    return QStringLiteral("Resets in %1d %2h").arg(seconds / 86400).arg(seconds % 86400 / 3600);
}

// The switcher hairline tracks each provider's secondary quota window.
static int secondaryRemaining(const ProviderUsage &usage, const ProviderInfo &info)
{
    for (const auto &metric : usage.metrics) {
        if (metric.label == info.secondaryLabel)
            return 100 - metric.usedPercent;
    }
    return -1;
}

UsageBarWidget::UsageBarWidget(QWidget *parent)
    : QWidget(parent), value_(0)
{
    setFixedHeight(kBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateAccessibility();
}

void UsageBarWidget::setValue(int value)
{
    const auto clamped = qBound(0, value, 100);
    if (clamped == value_)
        return;
    value_ = clamped;
    updateAccessibility();
    update();
}

void UsageBarWidget::setLabel(const QString &label)
{
    if (label == label_)
        return;
    label_ = label;
    updateAccessibility();
}

QSize UsageBarWidget::sizeHint() const
{
    return QSize(kBarWidthHint, kBarHeight);
}

void UsageBarWidget::updateAccessibility()
{
    setAccessibleName(label_.isEmpty()
                          ? QStringLiteral("%1% used").arg(value_)
                          : QStringLiteral("%1, %2% used").arg(label_).arg(value_));
    setAccessibleDescription(QStringLiteral("Usage bar"));
}

void UsageBarWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QRectF track(rect());
    const qreal radius = track.height() / 2.0;
    painter.setBrush(overlayColor(palette(), kTrackAlpha));
    painter.drawRoundedRect(track, radius, radius);

    if (value_ <= 0)
        return;

    const bool dark = isDark(palette());
    QColor fill;
    if (value_ < 75)
        fill = palette().color(QPalette::Highlight);
    else if (value_ < 90)
        fill = dark ? QColor(0xE5, 0x8E, 0x26).lighter(115) : QColor(0xE5, 0x8E, 0x26);
    else
        fill = dark ? QColor(0xF0, 0x6A, 0x6A) : QColor(0xD9, 0x3F, 0x3F);

    const qreal width = qMin(track.width(), qMax(track.width() * value_ / 100.0, track.height()));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(width, track.height())), radius, radius);
}

void UsageBarWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
        update();
    QWidget::changeEvent(event);
}

CostSparkline::CostSparkline(QWidget *parent)
    : QWidget(parent), max_(0)
{
    setFixedHeight(kSparkHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void CostSparkline::setDays(const QList<CostDay> &days)
{
    days_ = days;
    max_ = 0;
    for (const auto &day : days_)
        max_ = qMax(max_, day.cost);
    setAccessibleName(days_.isEmpty()
                          ? QStringLiteral("No cost history")
                          : QStringLiteral("Daily cost over %1 days, highest %2")
                                .arg(days_.size()).arg(formatCost(max_)));
    setAccessibleDescription(QStringLiteral("Cost history"));
    update();
}

QSize CostSparkline::sizeHint() const
{
    return QSize(kBarWidthHint, kSparkHeight);
}

int CostSparkline::dayAt(int x) const
{
    if (days_.isEmpty() || width() <= 0)
        return -1;
    const int index = x * days_.size() / width();
    return index >= 0 && index < days_.size() ? index : -1;
}

void CostSparkline::paintEvent(QPaintEvent *)
{
    // A flat-zero history carries no shape worth drawing, and dividing by a zero
    // max would scale every bar to full height.
    if (days_.isEmpty() || max_ <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    const auto accent = palette().color(QPalette::Highlight);
    auto faded = accent;
    faded.setAlphaF(kSparkAlpha);

    const qreal slot = qreal(width()) / days_.size();
    for (int index = 0; index < days_.size(); ++index) {
        const qreal barWidth = qMax(1.0, slot - kSparkGap);
        const qreal left = index * slot;
        const qreal barHeight = qMax(1.0, height() * days_[index].cost / max_);
        // The last day is today-so-far; accent it so a partial day is not read
        // as a drop in spend.
        painter.setBrush(index == days_.size() - 1 ? accent : faded);
        painter.drawRect(QRectF(left, height() - barHeight, barWidth, barHeight));
    }
}

void CostSparkline::mouseMoveEvent(QMouseEvent *event)
{
    const int index = dayAt(event->position().toPoint().x());
    if (index < 0) {
        QToolTip::hideText();
        return;
    }
    const auto &day = days_[index];
    QToolTip::showText(event->globalPosition().toPoint(),
                       QStringLiteral("%1 · %2 · %3")
                           .arg(day.date, formatCost(day.cost), formatTokens(day.tokens)),
                       this);
}

void CostSparkline::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
        update();
    QWidget::changeEvent(event);
}

LinkButton::LinkButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFont(scaledFont(font(), 0.9));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize LinkButton::sizeHint() const
{
    const QFontMetrics metrics(font());
    return QSize(metrics.horizontalAdvance(text()), metrics.height());
}

void LinkButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QFont drawn = font();
    drawn.setUnderline(hovered_ || hasFocus());
    painter.setFont(drawn);
    painter.setPen(hovered_ || hasFocus() ? palette().color(QPalette::Text) : mutedColor(palette()));
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, text());
}

void LinkButton::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void LinkButton::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

ProviderSegmentButton::ProviderSegmentButton(const ProviderInfo &info, QWidget *parent)
    : QAbstractButton(parent), info_(info)
{
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setText(info_.name);
    updateIcons();
    updateAccessibility();
}

void ProviderSegmentButton::setRemainingPercent(int percent)
{
    const int clamped = percent < 0 ? -1 : qBound(0, percent, 100);
    if (clamped == remaining_)
        return;
    remaining_ = clamped;
    updateAccessibility();
    update();
}

void ProviderSegmentButton::updateAccessibility()
{
    setAccessibleName(info_.name);
    setAccessibleDescription(remaining_ < 0
                                 ? QStringLiteral("Show %1 usage").arg(info_.name)
                                 : QStringLiteral("Show %1 usage, %2% of the %3 limit remaining")
                                       .arg(info_.name).arg(remaining_).arg(info_.secondaryLabel));
}

void ProviderSegmentButton::updateIcons()
{
    const qreal ratio = devicePixelRatioF();
    selectedIcon_ = tintedIcon(info_.icon, onAccentColor(palette()), kSegmentIconSize, ratio);
    unselectedIcon_ = tintedIcon(info_.icon, mutedColor(palette()), kSegmentIconSize, ratio);
}

QSize ProviderSegmentButton::sizeHint() const
{
    const QFontMetrics metrics(scaledFont(font(), kSegmentFontScale));
    return QSize(metrics.horizontalAdvance(text()) + 2 * kSegmentPaddingSide,
                 kSegmentPaddingTop + kSegmentIconSize + kSegmentIconGap + metrics.height()
                     + kSegmentPaddingBottom);
}

void ProviderSegmentButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QRectF plate(rect());
    if (isChecked())
        painter.setBrush(palette().color(QPalette::Highlight));
    else if (hovered_ || hasFocus() || isDown())
        painter.setBrush(overlayColor(palette(), kHoverAlpha));
    else
        painter.setBrush(Qt::NoBrush);
    if (painter.brush().style() != Qt::NoBrush)
        painter.drawRoundedRect(plate, kPlateRadius, kPlateRadius);

    const auto &icon = isChecked() ? selectedIcon_ : unselectedIcon_;
    painter.drawPixmap(QPointF((width() - kSegmentIconSize) / 2.0, kSegmentPaddingTop), icon);

    const QFont labelFont = scaledFont(font(), kSegmentFontScale);
    const QFontMetrics metrics(labelFont);
    painter.setFont(labelFont);
    const auto content = isChecked() ? onAccentColor(palette()) : mutedColor(palette());
    painter.setPen(content);
    const QRectF label(0, kSegmentPaddingTop + kSegmentIconSize + kSegmentIconGap, width(),
                       metrics.height());
    painter.drawText(label, Qt::AlignHCenter | Qt::AlignVCenter,
                     metrics.elidedText(text(), Qt::ElideRight, width() - 2 * kSegmentPaddingSide));

    // Only the unselected segments carry it: the selected provider's weekly bar
    // is already spelled out in full a few rows below.
    if (isChecked() || remaining_ < 0)
        return;
    const QRectF track(kQuotaSideInset, height() - kQuotaBottomInset - kQuotaHeight,
                       width() - 2 * kQuotaSideInset, kQuotaHeight);
    if (track.width() <= 0)
        return;
    const qreal radius = kQuotaHeight / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(overlayColor(palette(), 0.22));
    painter.drawRoundedRect(track, radius, radius);
    if (remaining_ == 0)
        return;
    painter.setBrush(QColor::fromRgb(info_.brandRgb));
    painter.drawRoundedRect(QRectF(track.topLeft(),
                                   QSizeF(track.width() * remaining_ / 100.0, track.height())),
                            radius, radius);
}

void ProviderSegmentButton::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        emit navigated(event->key() == Qt::Key_Left ? -1 : 1);
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

void ProviderSegmentButton::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ProviderSegmentButton::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ProviderSegmentButton::changeEvent(QEvent *event)
{
    // The pixmaps bake in both the tint and the screen's scale factor, so a theme
    // switch and a drag onto a differently-scaled monitor both invalidate them.
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    const bool scaleChanged = event->type() == QEvent::DevicePixelRatioChange;
#else
    constexpr bool scaleChanged = false;
#endif
    if (event->type() == QEvent::PaletteChange || scaleChanged) {
        updateIcons();
        update();
    }
    QAbstractButton::changeEvent(event);
}

ProviderSwitcher::ProviderSwitcher(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kSegmentSpacing);

    for (const auto &info : providerCatalog()) {
        auto *segment = new ProviderSegmentButton(info, this);
        const int index = segments_.size();
        // Equal stretch rather than equal fixed widths, so the segments stay
        // symmetric whatever the font metrics say about "Codex" versus "Copilot".
        layout->addWidget(segment, 1);
        segments_.append(segment);
        connect(segment, &QAbstractButton::clicked, this, [this, index] { setCurrentIndex(index); });
        connect(segment, &ProviderSegmentButton::navigated, this, [this, index](int delta) {
            setCurrentIndex(qBound(0, index + delta, segments_.size() - 1));
            focusCurrent();
        });
    }
    if (!segments_.isEmpty())
        segments_[0]->setChecked(true);
}

void ProviderSwitcher::setCurrentIndex(int index)
{
    if (index < 0 || index >= segments_.size())
        return;
    // Re-assert the check state even when the index is unchanged: clicking the
    // already-selected segment would otherwise toggle it off and leave the
    // switcher showing nothing as current.
    for (int position = 0; position < segments_.size(); ++position)
        segments_[position]->setChecked(position == index);
    if (index == current_)
        return;
    current_ = index;
    emit currentChanged(index);
}

void ProviderSwitcher::setRemainingPercent(int index, int percent)
{
    if (index >= 0 && index < segments_.size())
        segments_[index]->setRemainingPercent(percent);
}

void ProviderSwitcher::focusCurrent()
{
    if (current_ >= 0 && current_ < segments_.size())
        segments_[current_]->setFocus();
}

MenuActionRow::MenuActionRow(const QString &themeIcon, const QString &glyph, const QString &text,
                             QWidget *parent)
    : QAbstractButton(parent), glyph_(glyph)
{
    setText(text);
    setIcon(QIcon::fromTheme(themeIcon));
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateAccessibility();
}

void MenuActionRow::setDetailLines(const QStringList &lines)
{
    details_ = lines;
    updateAccessibility();
    updateGeometry();
    update();
}

void MenuActionRow::setChevron(bool visible)
{
    chevron_ = visible;
    update();
}

void MenuActionRow::setExpanded(bool expanded)
{
    expanded_ = expanded;
    updateAccessibility();
    update();
}

void MenuActionRow::updateAccessibility()
{
    setAccessibleName(text());
    QStringList description = details_;
    if (chevron_)
        description.prepend(expanded_ ? QStringLiteral("Expanded") : QStringLiteral("Collapsed"));
    setAccessibleDescription(description.join(QStringLiteral(", ")));
}

QSize MenuActionRow::sizeHint() const
{
    const QFontMetrics title(font());
    const QFontMetrics detail(scaledFont(font(), 0.85));
    return QSize(kCardPadding * 2 + kRowIconSize + kRowIconGap + title.horizontalAdvance(text()),
                 2 * kRowPadding + qMax(title.height(), kRowIconSize)
                     + details_.size() * detail.height());
}

void MenuActionRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (hovered_ || hasFocus() || isDown()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(overlayColor(palette(), isDown() ? kHoverAlpha * 2 : kHoverAlpha));
        painter.drawRoundedRect(QRectF(rect()).adjusted(4, 0, -4, 0), kPlateRadius, kPlateRadius);
    }

    const QFontMetrics title(font());
    const int titleHeight = qMax(title.height(), kRowIconSize);
    const QRectF iconRect(kCardPadding, kRowPadding + (titleHeight - kRowIconSize) / 2.0,
                          kRowIconSize, kRowIconSize);
    painter.setPen(palette().color(QPalette::Text));
    if (!icon().isNull()) {
        icon().paint(&painter, iconRect.toRect(), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    } else {
        // No icon theme (a bare X session, or a theme without this name): the
        // glyph keeps the row legible instead of leaving a blank gutter.
        painter.drawText(iconRect, Qt::AlignCenter, glyph_);
    }

    const int textLeft = kCardPadding + kRowIconSize + kRowIconGap;
    const int textRight = width() - kCardPadding - (chevron_ ? 14 : 0);
    const QRectF titleRect(textLeft, kRowPadding, qMax(0, textRight - textLeft), titleHeight);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                     title.elidedText(text(), Qt::ElideRight, qRound(titleRect.width())));

    const QFontMetrics detail(scaledFont(font(), 0.85));
    painter.setFont(scaledFont(font(), 0.85));
    painter.setPen(mutedColor(palette()));
    qreal top = kRowPadding + titleHeight;
    for (const auto &line : details_) {
        const QRectF lineRect(textLeft, top, qMax(0, textRight - textLeft), detail.height());
        painter.drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter,
                         detail.elidedText(line, Qt::ElideRight, qRound(lineRect.width())));
        top += detail.height();
    }

    if (!chevron_)
        return;
    painter.setPen(QPen(mutedColor(palette()), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QPointF pivot(width() - kCardPadding - 4, kRowPadding + titleHeight / 2.0);
    QPainterPath path;
    if (expanded_) {
        path.moveTo(pivot + QPointF(-4, -2));
        path.lineTo(pivot + QPointF(0, 2));
        path.lineTo(pivot + QPointF(4, -2));
    } else {
        path.moveTo(pivot + QPointF(-2, -4));
        path.lineTo(pivot + QPointF(2, 0));
        path.lineTo(pivot + QPointF(-2, 4));
    }
    painter.drawPath(path);
}

void MenuActionRow::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void MenuActionRow::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void MenuActionRow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
        update();
    QAbstractButton::changeEvent(event);
}

ProviderView::ProviderView(const ProviderInfo &info, QWidget *parent)
    : QWidget(parent), info_(info), name_(new QLabel(info.name, this)),
      account_(new QLabel(this)), meta_(new LinkButton(this)), body_(new QVBoxLayout)
{
    auto font = scaledFont(name_->font(), 1.1);
    font.setBold(true);
    name_->setFont(font);

    meta_->setToolTip(QStringLiteral("Refresh now (Ctrl+R)"));
    meta_->setAccessibleName(QStringLiteral("Refresh usage"));
    connect(meta_, &QAbstractButton::clicked, this, &ProviderView::refreshRequested);

    account_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    account_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    account_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(kCardPadding, 4, kCardPadding, 8);
    headerLayout->setSpacing(kHeaderSpacing);
    auto *left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(2);
    left->addWidget(name_);
    left->addWidget(meta_);
    headerLayout->addLayout(left);
    headerLayout->addStretch(1);
    headerLayout->addWidget(account_, 0, Qt::AlignBottom);
    layout->addWidget(header);
    layout->addWidget(makeDivider(this));

    body_->setSpacing(0);
    layout->addLayout(body_);
    layout->addStretch(1);

    setUsage(nullptr, {});
}

QLabel *ProviderView::addMuted(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    muted_.append(label);
    return label;
}

void ProviderView::addDivider()
{
    body_->addWidget(makeDivider(this));
}

void ProviderView::addSection(const QString &title)
{
    auto *label = addMuted(title, this);
    auto font = scaledFont(label->font(), 0.9);
    font.setBold(true);
    label->setFont(font);
    label->setContentsMargins(kCardPadding, 10, kCardPadding, 2);
    body_->addWidget(label);
}

void ProviderView::addDetailRow(const QString &label, const QString &value)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(kCardPadding, 2, kCardPadding, 2);
    layout->addWidget(addMuted(label, row));
    layout->addStretch();
    auto *right = new QLabel(value, row);
    right->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(right);
    body_->addWidget(row);
}

void ProviderView::addMetric(const UsageMetric &metric, const DisplayOptions &options)
{
    auto *row = new QWidget(this);
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(kCardPadding, 6, kCardPadding, 6);
    layout->setSpacing(5);

    auto *title = new QLabel(metric.label, row);
    layout->addWidget(title);

    auto *bar = new UsageBarWidget(row);
    bar->setLabel(metric.label);
    bar->setValue(metric.usedPercent);
    layout->addWidget(bar);

    // The bar always fills by consumption — its colour escalates with usage, so
    // inverting it would paint an almost-empty red bar at 95% used. Only the
    // number follows the preference.
    auto *caption = new QHBoxLayout;
    caption->setContentsMargins(0, 0, 0, 0);
    auto *used = new QLabel(options.showRemaining
                                ? QStringLiteral("%1% left").arg(100 - metric.usedPercent)
                                : QStringLiteral("%1% used").arg(metric.usedPercent),
                            row);
    auto usedFont = scaledFont(used->font(), 0.9);
    usedFont.setBold(true);
    used->setFont(usedFont);
    caption->addWidget(used);
    caption->addStretch();

    const auto countdown = options.showResetWhenExhausted && metric.usedPercent >= 100
        ? resetCountdown(metric)
        : metric.reset.isEmpty() ? QString() : QStringLiteral("Resets %1").arg(metric.reset);
    if (!countdown.isEmpty()) {
        auto *reset = addMuted(countdown, row);
        reset->setFont(scaledFont(reset->font(), 0.9));
        reset->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        caption->addWidget(reset);
    }
    layout->addLayout(caption);

    if (!metric.pace.isEmpty()) {
        auto *pace = addMuted(metric.pace, row);
        pace->setFont(scaledFont(pace->font(), 0.85));
        pace->setWordWrap(true);
        layout->addWidget(pace);
    }
    body_->addWidget(row);
}

void ProviderView::addCost(const UsageCost &cost, bool showComparisons)
{
    if (cost.daily.isEmpty() && cost.last30DaysCost <= 0 && cost.todayCost <= 0)
        return;

    addDivider();

    // The comparisons and the chart stay behind the chevron: they are the reason
    // the old Cost block swamped everything above it.
    auto *detail = new QWidget(this);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(kCardPadding + kRowIconSize + kRowIconGap, 0, kCardPadding, 6);
    detailLayout->setSpacing(4);
    const auto addLine = [this, detailLayout, detail](const QString &label, double value,
                                                      qint64 tokens) {
        auto *line = addMuted(QStringLiteral("%1  %2 · %3")
                                  .arg(label, formatCost(value), formatTokens(tokens)),
                              detail);
        line->setFont(scaledFont(line->font(), 0.85));
        detailLayout->addWidget(line);
    };
    if (showComparisons) {
        addLine(QStringLiteral("Last 7 days"), cost.last7DaysCost, cost.last7DaysTokens);
        addLine(QStringLiteral("Last 90 days"), cost.last90DaysCost, cost.last90DaysTokens);
    }
    // One bar is not a history, and the widget draws nothing for a flat zero
    // series anyway.
    if (cost.daily.size() >= 2) {
        auto *spark = new CostSparkline(detail);
        spark->setDays(cost.daily);
        detailLayout->addWidget(spark);
    }

    auto *row = new MenuActionRow(QStringLiteral("office-chart-bar"), QStringLiteral("$"),
                                  QStringLiteral("Cost"), this);
    // With comparisons off and no history there is nothing behind the chevron, so
    // it would only promise a disclosure that never arrives.
    const bool disclosable = !detailLayout->isEmpty();
    row->setChevron(disclosable);
    row->setExpanded(costExpanded_ && disclosable);
    row->setDetailLines({QStringLiteral("Today  %1 · %2")
                             .arg(formatCost(cost.todayCost), formatTokens(cost.todayTokens)),
                         QStringLiteral("Last 30 days  %1 · %2")
                             .arg(formatCost(cost.last30DaysCost),
                                  formatTokens(cost.last30DaysTokens))});
    body_->addWidget(row);

    body_->addWidget(detail);
    detail->setVisible(costExpanded_ && disclosable);
    if (!disclosable)
        return;
    connect(row, &QAbstractButton::clicked, this, [this, row, detail] {
        costExpanded_ = !costExpanded_;
        row->setExpanded(costExpanded_);
        detail->setVisible(costExpanded_);
    });
}

void ProviderView::addActions()
{
    addDivider();

    if (!info_.accountUrl.isEmpty()) {
        // CodexBarCLI has no login command, so there is no account flow to drive
        // from here. Opening the provider's own account page is the honest
        // version of upstream's Add/Switch Account, and the row says so.
        auto *account = new MenuActionRow(QStringLiteral("system-users"), QStringLiteral("@"),
                                          QStringLiteral("Manage Account…"), this);
        account->setToolTip(QStringLiteral("Opens %1 in your browser")
                                .arg(QUrl(info_.accountUrl).host()));
        connect(account, &QAbstractButton::clicked, this,
                [this] { QDesktopServices::openUrl(QUrl(info_.accountUrl)); });
        body_->addWidget(account);
    }

    if (!info_.dashboardUrl.isEmpty()) {
        auto *dashboard = new MenuActionRow(QStringLiteral("utilities-system-monitor"),
                                            QStringLiteral("↗"), QStringLiteral("Usage Dashboard"),
                                            this);
        connect(dashboard, &QAbstractButton::clicked, this,
                [this] { QDesktopServices::openUrl(QUrl(info_.dashboardUrl)); });
        body_->addWidget(dashboard);
    }

    if (!statusUrl_.isEmpty()) {
        auto *status = new MenuActionRow(QStringLiteral("network-server"), QStringLiteral("●"),
                                         QStringLiteral("Status Page"), this);
        if (!statusText_.isEmpty())
            status->setToolTip(statusText_);
        const auto url = statusUrl_;
        connect(status, &QAbstractButton::clicked, this,
                [url] { QDesktopServices::openUrl(QUrl(url)); });
        body_->addWidget(status);
    }
}

void ProviderView::setUsage(const ProviderUsage *usage, const DisplayOptions &options)
{
    while (auto *item = body_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    muted_ = {account_};

    accountText_ = usage && !usage->plan.isEmpty() ? usage->plan
        : usage && !usage->account.isEmpty()        ? usage->account
                                                    : QStringLiteral("OAuth account");
    account_->setToolTip(usage && !usage->account.isEmpty() ? usage->account : accountText_);
    statusUrl_ = usage && !usage->statusUrl.isEmpty() ? usage->statusUrl : info_.statusUrl;
    statusText_ = usage ? usage->status : QString();

    metaText_ = usage && !usage->updatedAt.isEmpty()
        ? QStringLiteral("Updated %1").arg(formatUpdated(usage->updatedAt))
        : QStringLiteral("Refresh");
    meta_->setText(metaText_);

    if (usage && !usage->error.isEmpty()) {
        auto *label = new QLabel(usage->error, this);
        label->setWordWrap(true);
        label->setContentsMargins(kCardPadding, 8, kCardPadding, 8);
        body_->addWidget(label);
    }
    if (!usage || usage->metrics.isEmpty()) {
        auto *label = addMuted(usage && !usage->error.isEmpty() ? QStringLiteral("No usage windows reported")
                                                                : QStringLiteral("Waiting for usage data"),
                               this);
        label->setContentsMargins(kCardPadding, 8, kCardPadding, 8);
        body_->addWidget(label);
        if (usage && !usage->error.isEmpty())
            accountText_ = QStringLiteral("Unavailable");
    } else {
        // Extra rate windows are already folded into metrics by parseUsage, so
        // they render as ordinary usage rows in the order the CLI reported them.
        for (const auto &metric : usage->metrics)
            addMetric(metric, options);

        if (usage->resetCredits > 0) {
            addSection(QStringLiteral("Reset credits"));
            addDetailRow(QStringLiteral("Available"), QString::number(usage->resetCredits));
            for (const auto &credit : usage->resetCreditDetails) {
                addDetailRow(credit.title, credit.expiresAt.isEmpty()
                    ? QStringLiteral("Does not expire")
                    : QStringLiteral("Expires %1").arg(formatStamp(credit.expiresAt)));
            }
        }
    }

    if (usage && options.showCost)
        addCost(usage->cost, options.showCostComparisons);
    addActions();

    applyMutedColor();
    elideAccount();
}

void ProviderView::setBusy(bool busy)
{
    meta_->setEnabled(!busy);
    meta_->setText(busy ? QStringLiteral("Refreshing…") : metaText_);
}

void ProviderView::applyMutedColor()
{
    const auto color = mutedColor(palette());
    for (auto *label : muted_)
        setMutedColor(label, color);
}

void ProviderView::elideAccount()
{
    const int available = qMax(0, contentsRect().width() / 2 - 2 * kCardPadding);
    if (available <= 0) {
        account_->setText(QString());
        return;
    }
    account_->setText(QFontMetrics(account_->font()).elidedText(accountText_, Qt::ElideMiddle, available));
}

void ProviderView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    elideAccount();
}

void ProviderView::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange) {
        applyMutedColor();
        update();
    }
    QWidget::changeEvent(event);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), popover_(QSystemTrayIcon::isSystemTrayAvailable()),
      switcher_(new ProviderSwitcher), stack_(new QStackedWidget)
{
    setWindowTitle(QStringLiteral("UsageBar"));
    setWindowIcon(QIcon::fromTheme("usagebar", QIcon(QStringLiteral(":/assets/usagebar-256.png"))));

    if (popover_) {
        // Deliberately not Qt::Popup: unparented popups map to an xdg_popup,
        // which needs a parent surface and misbehaves on Wayland. A tool window
        // that hides on deactivation gives the same dismissal behavior.
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        auto *escape = new QShortcut(Qt::Key_Escape, this);
        escape->setContext(Qt::WindowShortcut);
        connect(escape, &QShortcut::activated, this, &MainWindow::hide);
        setFixedSize(popoverSize());
    } else {
        // Without a tray this is an ordinary window, so it stays resizable. It
        // still opens at the card's ideal size rather than hugging its content.
        setMinimumSize(360, 320);
        resize(popoverSize());
    }

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    // The panel is drawn with rounded corners, so the outer margins are what keep
    // the switcher and the Quit row from painting over them.
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(0);

    auto *switcherRow = new QWidget(central);
    auto *switcherLayout = new QVBoxLayout(switcherRow);
    switcherLayout->setContentsMargins(kCardPadding - 4, 10, kCardPadding - 4, 8);
    switcherLayout->addWidget(switcher_);
    layout->addWidget(switcherRow);

    for (const auto &info : providerCatalog()) {
        auto *view = new ProviderView(info);
        connect(view, &ProviderView::refreshRequested, this, &MainWindow::refreshRequested);
        views_.append(view);
        stack_->addWidget(view);
    }

    // The ideal card is taller than a laptop screen with a panel on it, so the
    // body scrolls rather than letting the popover run off the display.
    auto *scroll = new QScrollArea(central);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    stack_->setAutoFillBackground(false);
    scroll->setWidget(stack_);
    layout->addWidget(scroll, 1);

    layout->addWidget(makeDivider(central));
    auto *settings = new MenuActionRow(QStringLiteral("preferences-system"), QStringLiteral("⚙"),
                                       QStringLiteral("Settings…"), central);
    auto *about = new MenuActionRow(QStringLiteral("help-about"), QStringLiteral("ⓘ"),
                                    QStringLiteral("About UsageBar"), central);
    auto *quit = new MenuActionRow(QStringLiteral("application-exit"), QStringLiteral("✕"),
                                   QStringLiteral("Quit"), central);
    layout->addWidget(settings);
    layout->addWidget(about);
    layout->addWidget(quit);
    setCentralWidget(central);

    connect(settings, &QAbstractButton::clicked, this, &MainWindow::settingsRequested);
    connect(about, &QAbstractButton::clicked, this, &MainWindow::aboutRequested);
    connect(quit, &QAbstractButton::clicked, qApp, &QApplication::quit);
    connect(switcher_, &ProviderSwitcher::currentChanged, this, &MainWindow::selectProvider);

    // Spelled out rather than taken from QKeySequence's standard keys: Refresh
    // resolves to F5 on X11 and Preferences is simply unbound off macOS.
    auto *refreshShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this);
    connect(refreshShortcut, &QShortcut::activated, this, &MainWindow::refreshRequested);
    auto *settingsShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+,")), this);
    connect(settingsShortcut, &QShortcut::activated, this, &MainWindow::settingsRequested);
    auto *quitShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), this);
    connect(quitShortcut, &QShortcut::activated, qApp, &QApplication::quit);
    for (int index = 0; index < providerCatalog().size(); ++index) {
        auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + index)), this);
        connect(shortcut, &QShortcut::activated, this, [this, index] { selectProvider(index); });
    }

    const auto stored = QSettings().value("ui/provider").toString();
    int initial = 0;
    for (int index = 0; index < providerCatalog().size(); ++index) {
        if (providerCatalog()[index].id == stored)
            initial = index;
    }
    selectProvider(initial);
}

// Reentrant by way of the switcher's currentChanged: ProviderSwitcher assigns its
// index before it emits, so the setCurrentIndex() below is a no-op the second
// time round rather than a loop.
void MainWindow::selectProvider(int index)
{
    switcher_->setCurrentIndex(index);
    const int current = switcher_->currentIndex();
    stack_->setCurrentIndex(current);
    // QStackedLayout hints at its tallest page, so a short provider would still
    // scroll for as long as the other one needs. Ignored drops the hidden page
    // out of that calculation and leaves the scroll extent honest.
    for (int position = 0; position < views_.size(); ++position) {
        auto policy = views_[position]->sizePolicy();
        policy.setVerticalPolicy(position == current ? QSizePolicy::Preferred
                                                     : QSizePolicy::Ignored);
        views_[position]->setSizePolicy(policy);
    }
    QSettings().setValue("ui/provider", providerCatalog()[current].id);
}

QSize MainWindow::popoverSize() const
{
    auto *screen = QGuiApplication::screenAt(anchor_.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return QSize(kPopoverWidth, kPopoverHeight);
    const auto available = screen->availableGeometry();
    return QSize(qMin(kPopoverWidth, available.width() - 2 * kScreenMargin),
                 qMin(kPopoverHeight, available.height() - 2 * kScreenMargin));
}

void MainWindow::positionAt()
{
    setFixedSize(popoverSize());

    auto *screen = QGuiApplication::screenAt(anchor_.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    // A Wayland client cannot place its own xdg_toplevel: KWin silently ignores
    // the request and centers the window, and QCursor::pos() returns (0,0)
    // because the pointer cannot be queried without focus. Positioning anyway
    // would only desync Qt's idea of the geometry from the compositor's, which
    // then misplaces tooltips and menus. Let the compositor place it instead.
    // X11 and XWayland honor the request, so anchoring still works there.
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")) || !screen)
        return;

    const QRect bounds = screen->availableGeometry().adjusted(kScreenMargin, kScreenMargin,
                                                              -kScreenMargin, -kScreenMargin);

    QRect rect(QPoint(0, 0), size());
    rect.moveCenter(anchor_.center());
    // Drop below a top-half anchor, rise above a bottom-half one, so the
    // popover grows away from the panel the tray icon lives in.
    if (anchor_.center().y() < screen->availableGeometry().center().y())
        rect.moveTop(anchor_.bottom() + kAnchorGap);
    else
        rect.moveBottom(anchor_.top() - kAnchorGap);

    rect.moveLeft(qBound(bounds.left(), rect.left(), bounds.right() - rect.width() + 1));
    rect.moveTop(qBound(bounds.top(), rect.top(), bounds.bottom() - rect.height() + 1));
    move(rect.topLeft());
}

void MainWindow::showAt(const QRect &iconGeometry)
{
    if (popover_) {
        // QSystemTrayIcon::geometry() is empty under SNI, so only use it when it
        // is actually populated and fall back to the pointer otherwise.
        const bool haveIcon = iconGeometry.isValid() && !iconGeometry.isEmpty();
        anchor_ = haveIcon ? iconGeometry : QRect(QCursor::pos(), QSize(1, 1));
        positionAt();
    }
    showNormal();
    raise();
    activateWindow();
    // Park focus on the switcher each open: it makes left/right selection work
    // straight away, and keeps the focus plate off whichever menu row the
    // compositor's async focus grant would otherwise land on.
    switcher_->focusCurrent();
    if (refreshOnOpen_)
        emit refreshRequested();
}

void MainWindow::toggle(const QRect &iconGeometry)
{
    if (isVisible()) {
        hide();
        return;
    }
    // Clicking the tray icon while the popover is open deactivates it, so it has
    // already hidden itself by the time activated() reaches us. Re-showing here
    // would make the popover impossible to close, so swallow that activation.
    if (popover_ && hiddenAt_.isValid() && hiddenAt_.elapsed() < kDismissGuardMs) {
        hiddenAt_.invalidate();
        return;
    }
    showAt(iconGeometry);
}

void MainWindow::hideEvent(QHideEvent *event)
{
    hiddenAt_.start();
    QMainWindow::hideEvent(event);
}

bool MainWindow::event(QEvent *event)
{
    // Stands in for Qt::Popup's implicit dismissal: clicking anywhere else, or
    // on the tray icon, takes focus away and closes the popover.
    if (popover_ && event->type() == QEvent::WindowDeactivate && isVisible())
        hide();
    return QMainWindow::event(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    if (!popover_) {
        QMainWindow::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    auto border = palette().color(QPalette::Text);
    border.setAlphaF(kPanelBorderAlpha);
    painter.setBrush(palette().color(QPalette::Window));
    painter.setPen(QPen(border, 1));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kPanelRadius, kPanelRadius);
}

void MainWindow::render()
{
    const auto &catalog = providerCatalog();
    for (int index = 0; index < catalog.size(); ++index) {
        const ProviderUsage *match = nullptr;
        for (const auto &provider : providers_) {
            if (provider.id == catalog[index].id)
                match = &provider;
        }
        views_[index]->setUsage(match, options_);
        switcher_->setRemainingPercent(
            index, match ? secondaryRemaining(*match, catalog[index]) : -1);
    }
}

void MainWindow::setUsage(const QList<ProviderUsage> &providers, const QString &)
{
    providers_ = providers;
    render();
}

void MainWindow::setDisplayOptions(const DisplayOptions &options)
{
    options_ = options;
    render();
}

void MainWindow::setRefreshOnOpen(bool enabled)
{
    refreshOnOpen_ = enabled;
}

void MainWindow::setBusy(bool busy)
{
    for (auto *view : views_)
        view->setBusy(busy);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    hide();
}
