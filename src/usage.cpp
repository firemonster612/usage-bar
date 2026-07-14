#include "usage.h"
#include "providers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDate>

static void addMetric(QList<UsageMetric> &metrics, const QString &label, const QJsonValue &value,
                      const QJsonValue &paceValue = {})
{
    if (!value.isObject())
        return;
    const auto window = value.toObject();
    if (window.value("isSyntheticPlaceholder").toBool()
        || !window.value("usedPercent").isDouble())
        return;
    metrics.append({label,
                    qBound(0, window.value("usedPercent").toInt(), 100),
                    window.value("resetDescription").toString(),
                    window.value("resetsAt").toString(window.value("resetDescription").toString()),
                    paceValue.toObject().value("summary").toString()});
}

QList<ProviderUsage> parseUsage(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error)
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("Expected a provider array")
                : parseError.errorString();
        return {};
    }

    QList<ProviderUsage> providers;
    for (const auto &value : document.array()) {
        if (!value.isObject())
            continue;
        const auto object = value.toObject();
        if (object.value("isSyntheticPlaceholder").toBool())
            continue;
        const auto id = object.value("provider").toString();
        const auto usageValue = object.value("usage");
        const auto *info = providerInfo(id);
        if (!info)
            continue;

        const auto &name = info->name;
        if (!usageValue.isObject()) {
            const auto message = object.value("error").toObject().value("message").toString();
            if (!message.isEmpty())
                providers.append({id, name, {}, {}, message});
            continue;
        }

        const auto usage = usageValue.toObject();
        if (usage.value("isSyntheticPlaceholder").toBool())
            continue;
        const auto identity = usage.value("identity").toObject();
        ProviderUsage provider{id,
                               name,
                               identity.value("accountEmail").toString(),
                               {},
                               object.value("error").toObject().value("message").toString()};
        if (provider.account.isEmpty())
            provider.account = usage.value("accountEmail").toString();
        if (provider.account.isEmpty())
            provider.account = identity.value("loginMethod").toString();

        provider.plan = identity.value("loginMethod").toString(usage.value("loginMethod").toString());
        if (provider.plan == QLatin1String("prolite"))
            provider.plan = QStringLiteral("Pro");
        else if (provider.plan.startsWith(QLatin1String("Claude ")))
            provider.plan.remove(0, 7);

        provider.updatedAt = usage.value("updatedAt").toString();
        provider.source = object.value("source").toString();
        provider.version = object.value("version").toString();
        const auto status = object.value("status").toObject();
        provider.status = status.value("description").toString();
        provider.statusUrl = status.value("url").toString();
        const auto resetCredits = usage.value("codexResetCredits").toObject();
        provider.resetCredits = resetCredits.value("availableCount").toInt();
        for (const auto &creditValue : resetCredits.value("credits").toArray()) {
            const auto credit = creditValue.toObject();
            if (credit.value("status").toString() != QLatin1String("available"))
                continue;
            provider.resetCreditDetails.append({credit.value("title").toString(QStringLiteral("Full reset")),
                                                credit.value("expires_at").toString()});
        }
        const auto pace = object.value("pace").toObject();
        addMetric(provider.metrics, info->primaryLabel,
                  usage.value("primary"), pace.value("primary"));
        addMetric(provider.metrics, info->secondaryLabel, usage.value("secondary"),
                  pace.value("secondary"));
        addMetric(provider.metrics, info->tertiaryLabel, usage.value("tertiary"),
                  pace.value("tertiary"));
        for (const auto &extraValue : usage.value("extraRateWindows").toArray()) {
            const auto extra = extraValue.toObject();
            const auto window = extra.value("window").toObject();
            if ((extra.contains("usageKnown") && !extra.value("usageKnown").toBool())
                || (window.contains("usageKnown") && !window.value("usageKnown").toBool()))
                continue;
            addMetric(provider.metrics, extra.value("title").toString(QStringLiteral("Additional")),
                      extra.value("window"));
        }
        providers.append(provider);
    }
    if (error)
        error->clear();
    return providers;
}

QHash<QString, UsageCost> parseCosts(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error)
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("Expected a cost array")
                : parseError.errorString();
        return {};
    }

    QHash<QString, UsageCost> costs;
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        const auto id = object.value("provider").toString();
        if (id != "codex" && id != "claude")
            continue;
        UsageCost cost;
        cost.last30DaysCost = object.value("last30DaysCostUSD").toDouble();
        cost.last30DaysTokens = object.value("last30DaysTokens").toInteger();
        for (const auto &dayValue : object.value("daily").toArray()) {
            const auto day = dayValue.toObject();
            CostDay point{day.value("date").toString(), day.value("totalCost").toDouble(),
                          day.value("totalTokens").toInteger()};
            cost.daily.append(point);
        }
        if (!cost.daily.isEmpty()) {
            const auto &today = cost.daily.last();
            cost.todayCost = today.cost;
            cost.todayTokens = today.tokens;
            const auto latest = QDate::fromString(today.date, Qt::ISODate);
            for (const auto &point : cost.daily) {
                const auto date = QDate::fromString(point.date, Qt::ISODate);
                const auto age = date.daysTo(latest);
                if (!latest.isValid() || !date.isValid() || (age >= 0 && age <= 89)) {
                    cost.last90DaysCost += point.cost;
                    cost.last90DaysTokens += point.tokens;
                }
                if (!latest.isValid() || !date.isValid() || (age >= 0 && age <= 6)) {
                    cost.last7DaysCost += point.cost;
                    cost.last7DaysTokens += point.tokens;
                }
            }
        }
        costs.insert(id, cost);
    }
    if (error)
        error->clear();
    return costs;
}
