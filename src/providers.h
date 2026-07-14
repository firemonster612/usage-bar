#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

struct ProviderInfo {
    QString id;
    QString name;
    QString source;
    QString primaryLabel;
    QString secondaryLabel;
    QString tertiaryLabel;
    QString icon;
    quint32 brandRgb;
    QString dashboardUrl;
    QString statusUrl;
    QString accountUrl;
};

const QList<ProviderInfo> &providerCatalog();
const ProviderInfo *providerInfo(const QString &id);
