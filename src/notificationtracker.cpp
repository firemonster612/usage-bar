#include "notificationtracker.h"

QList<UsageAlert> NotificationTracker::update(const QList<ProviderUsage> &providers,
                                               const QList<int> &thresholds)
{
    QList<UsageAlert> alerts;
    auto baseline = [&thresholds](int usedPercent, const QString &cycle) {
        QSet<int> passed;
        for (const auto threshold : thresholds) {
            if (usedPercent >= threshold)
                passed.insert(threshold);
        }
        return WindowState{usedPercent, cycle, passed};
    };
    for (const auto &provider : providers) {
        for (const auto &metric : provider.metrics) {
            const auto key = provider.id + QChar(0x1f) + metric.label;
            auto state = windows_.find(key);
            if (state == windows_.end()) {
                windows_.insert(key, baseline(metric.usedPercent, metric.cycle));
                continue;
            }
            if (state->cycle != metric.cycle || metric.usedPercent < state->usedPercent) {
                *state = baseline(metric.usedPercent, metric.cycle);
                continue;
            }
            for (const auto threshold : thresholds) {
                if (state->usedPercent < threshold && metric.usedPercent >= threshold
                    && !state->notified.contains(threshold)) {
                    alerts.append({provider.name, metric.label, threshold, metric.usedPercent});
                    state->notified.insert(threshold);
                }
            }
            state->usedPercent = metric.usedPercent;
        }
    }
    return alerts;
}
