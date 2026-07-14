#pragma once

#include "usage.h"

#include <QHash>
#include <QObject>
#include <QStringList>

class QProcess;
class QTimer;

class UsageBackend final : public QObject {
    Q_OBJECT
public:
    explicit UsageBackend(QObject *parent = nullptr);
    void refresh();
    static QString findCli();

signals:
    void updated(const QList<ProviderUsage> &providers, const QString &message);
    void busyChanged(bool busy);

private:
    enum class Stage { Usage, Cost };

    void startProvider();
    void startCost();
    void finishUsageProvider(const QByteArray &output, const QString &stderrText);
    void finishRefresh();
    void finish();
    QProcess *process_;
    QTimer *timeout_;
    QHash<QString, ProviderUsage> lastGood_;
    QHash<QString, ProviderUsage> latest_;
    QList<ProviderUsage> pendingProviders_;
    QString pendingMessage_;
    QStringList refreshErrors_;
    int providerIndex_ = 0;
    Stage stage_ = Stage::Usage;
    bool inFlight_ = false;
    bool timedOut_ = false;
    bool failedToStart_ = false;
};
