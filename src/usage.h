#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

struct UsageMetric {
    QString label;
    int usedPercent = 0;
    QString reset;
    QString cycle;
    QString pace;
};

struct CostDay {
    QString date;
    double cost = 0;
    qint64 tokens = 0;
};

struct UsageCost {
    double todayCost = 0;
    qint64 todayTokens = 0;
    double last7DaysCost = 0;
    qint64 last7DaysTokens = 0;
    double last30DaysCost = 0;
    qint64 last30DaysTokens = 0;
    double last90DaysCost = 0;
    qint64 last90DaysTokens = 0;
    QList<CostDay> daily;
};

struct ResetCredit {
    QString title;
    QString expiresAt;
};

struct ProviderUsage {
    QString id;
    QString name;
    QString account;
    QList<UsageMetric> metrics;
    QString error;
    QString updatedAt;
    QString source;
    QString version;
    QString status;
    QString statusUrl;
    int resetCredits = 0;
    QList<ResetCredit> resetCreditDetails;
    UsageCost cost;
    QString plan;
};

QList<ProviderUsage> parseUsage(const QByteArray &json, QString *error = nullptr);
QHash<QString, UsageCost> parseCosts(const QByteArray &json, QString *error = nullptr);
