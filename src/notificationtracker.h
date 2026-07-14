#pragma once

#include "usage.h"

#include <QHash>
#include <QSet>

struct UsageAlert {
    QString provider;
    QString window;
    int threshold;
    int usedPercent;
};

class NotificationTracker final {
public:
    QList<UsageAlert> update(const QList<ProviderUsage> &providers, const QList<int> &thresholds);

private:
    struct WindowState {
        int usedPercent;
        QString cycle;
        QSet<int> notified;
    };
    QHash<QString, WindowState> windows_;
};
