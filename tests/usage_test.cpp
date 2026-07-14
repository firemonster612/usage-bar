#include "usage.h"
#include "notificationtracker.h"
#include "singleinstance.h"
#include "usagebackend.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class UsageTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesProvidersAndSkipsNullWindows();
    void rejectsNonArray();
    void notificationsCrossOnceAndReset();
    void skipsPlaceholdersAndUnknownWindows();
    void parsesAdditionalProviders();
    void activatesExistingInstance();
    void failedBackendStartCompletes();
    void parsesPartialProviderError();
    void parsesProviderDetailsAndCosts();
};

void UsageTest::parsesProvidersAndSkipsNullWindows()
{
    const auto providers = parseUsage(R"json([
        {"provider":"codex","usage":{"identity":{"accountEmail":"codex@example.com"},
         "primary":null,"secondary":{"usedPercent":15,"resetDescription":"Sunday"}}},
        {"provider":"claude","usage":{"identity":{"loginMethod":"Max"},
         "primary":{"usedPercent":95},"secondary":null,
         "extraRateWindows":[{"title":"Fable only","window":{"usedPercent":46}}]}},
        null
    ])json");

    QCOMPARE(providers.size(), 2);
    QCOMPARE(providers[0].account, QStringLiteral("codex@example.com"));
    QVERIFY(providers[0].plan.isEmpty());
    QCOMPARE(providers[0].metrics.size(), 1);
    QCOMPARE(providers[0].metrics[0].label, QStringLiteral("Weekly"));
    QCOMPARE(providers[1].account, QStringLiteral("Max"));
    QCOMPARE(providers[1].plan, QStringLiteral("Max"));
    QCOMPARE(providers[1].metrics.size(), 2);
    QCOMPARE(providers[1].metrics[1].label, QStringLiteral("Fable only"));
}

void UsageTest::rejectsNonArray()
{
    QString error;
    QVERIFY(parseUsage("{}", &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void UsageTest::notificationsCrossOnceAndReset()
{
    NotificationTracker tracker;
    auto sample = [](int used, const QString &cycle) {
        return QList<ProviderUsage>{{"codex", "Codex", {}, {{"Weekly", used, {}, cycle}}}};
    };
    const QList<int> thresholds{75, 90, 100};
    QVERIFY(tracker.update(sample(80, "one"), thresholds).isEmpty());
    QCOMPARE(tracker.update(sample(95, "one"), thresholds).size(), 1);
    QVERIFY(tracker.update(sample(95, "one"), thresholds).isEmpty());
    QVERIFY(tracker.update(sample(70, "one"), thresholds).isEmpty());
    QCOMPARE(tracker.update(sample(95, "one"), thresholds).size(), 2);
    QVERIFY(tracker.update(sample(80, "two"), thresholds).isEmpty());
    const auto alerts = tracker.update(sample(100, "two"), thresholds);
    QCOMPARE(alerts.size(), 2);
    QCOMPARE(alerts[0].threshold, 90);
    QCOMPARE(alerts[1].threshold, 100);
}

void UsageTest::skipsPlaceholdersAndUnknownWindows()
{
    const auto providers = parseUsage(R"json([
      {"provider":"codex","usage":{"primary":{"usedPercent":99,"isSyntheticPlaceholder":true}}},
      {"provider":"claude","usage":{"primary":{"usedPercent":10},"extraRateWindows":[
        {"title":"Unknown","usageKnown":false,"window":{"usedPercent":88}},
        {"title":"Known","window":{"usedPercent":20}}
      ]}}
    ])json");
    QCOMPARE(providers.size(), 2);
    QVERIFY(providers[0].metrics.isEmpty());
    QCOMPARE(providers[1].metrics.size(), 2);
    QCOMPARE(providers[1].metrics[1].label, QStringLiteral("Known"));
}

void UsageTest::parsesAdditionalProviders()
{
    const auto providers = parseUsage(R"json([
      {"provider":"cursor","usage":{"primary":{"usedPercent":10}}},
      {"provider":"factory","usage":{"secondary":{"usedPercent":20}}},
      {"provider":"gemini","usage":{"tertiary":{"usedPercent":30}}},
      {"provider":"copilot","usage":{"primary":{"usedPercent":40}}}
    ])json");
    QCOMPARE(providers.size(), 4);
    QCOMPARE(providers[0].name, QStringLiteral("Cursor"));
    QCOMPARE(providers[0].metrics[0].label, QStringLiteral("Total"));
    QCOMPARE(providers[1].name, QStringLiteral("Droid"));
    QCOMPARE(providers[1].metrics[0].label, QStringLiteral("Premium"));
    QCOMPARE(providers[2].metrics[0].label, QStringLiteral("Flash Lite"));
    QCOMPARE(providers[3].metrics[0].label, QStringLiteral("Premium"));
}

void UsageTest::parsesPartialProviderError()
{
    const auto providers = parseUsage(R"json([
      {"provider":"codex","usage":{"secondary":{"usedPercent":12}}},
      {"provider":"claude","error":{"message":"OAuth session expired"}}
    ])json");
    QCOMPARE(providers.size(), 2);
    QCOMPARE(providers[0].id, QStringLiteral("codex"));
    QCOMPARE(providers[0].metrics[0].usedPercent, 12);
    QCOMPARE(providers[1].id, QStringLiteral("claude"));
    QCOMPARE(providers[1].error, QStringLiteral("OAuth session expired"));
    QVERIFY(providers[1].metrics.isEmpty());
}

void UsageTest::parsesProviderDetailsAndCosts()
{
    const auto providers = parseUsage(R"json([{
      "provider":"codex","source":"oauth","version":"1.2.3",
      "status":{"description":"All Systems Operational","url":"https://status.example/"},
      "pace":{"secondary":{"summary":"On pace | Lasts until reset"}},
      "usage":{"updatedAt":"2026-07-14T09:18:08Z",
        "secondary":{"usedPercent":41,"resetDescription":"Sunday"},
        "codexResetCredits":{"availableCount":4,"credits":[
          {"title":"Full reset","status":"available","expires_at":"2026-08-01T00:00:00Z"},
          {"title":"Used reset","status":"used"}]}}
    }])json");
    QCOMPARE(providers.size(), 1);
    QCOMPARE(providers[0].source, QStringLiteral("oauth"));
    QCOMPARE(providers[0].status, QStringLiteral("All Systems Operational"));
    QCOMPARE(providers[0].metrics[0].pace, QStringLiteral("On pace | Lasts until reset"));
    QCOMPARE(providers[0].resetCredits, 4);
    QCOMPARE(providers[0].resetCreditDetails.size(), 1);
    QCOMPARE(providers[0].resetCreditDetails[0].expiresAt, QStringLiteral("2026-08-01T00:00:00Z"));

    const auto costs = parseCosts(R"json([{
      "provider":"codex","last30DaysCostUSD":12.5,"last30DaysTokens":2000,
      "daily":[{"date":"2026-04-01","totalCost":5,"totalTokens":1000},
               {"date":"2026-07-08","totalCost":2,"totalTokens":200},
               {"date":"2026-07-14","totalCost":1.25,"totalTokens":300}]
    }])json");
    QCOMPARE(costs.size(), 1);
    QCOMPARE(costs["codex"].todayCost, 1.25);
    QCOMPARE(costs["codex"].todayTokens, 300);
    QCOMPARE(costs["codex"].last30DaysCost, 12.5);
    QCOMPARE(costs["codex"].last7DaysCost, 3.25);
    QCOMPARE(costs["codex"].last90DaysCost, 3.25);
}

void UsageTest::activatesExistingInstance()
{
    const auto name = QStringLiteral("usagebar-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    SingleInstance primary(name);
    QVERIFY(primary.start(false));
    QSignalSpy shown(&primary, &SingleInstance::showRequested);
    SingleInstance background(name);
    QVERIFY(!background.start(false));
    QCoreApplication::processEvents();
    QCOMPARE(shown.size(), 0);
    SingleInstance foreground(name);
    QVERIFY(!foreground.start(true));
    QTRY_COMPARE(shown.size(), 1);
}

void UsageTest::failedBackendStartCompletes()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("broken-cli");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/definitely/missing/interpreter\n");
    file.close();
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    qputenv("USAGEBAR_CODEXBAR_CLI", path.toUtf8());
    UsageBackend backend;
    QSignalSpy busy(&backend, &UsageBackend::busyChanged);
    backend.refresh();
    QTRY_COMPARE(busy.size(), 2);
    QCOMPARE(busy[0][0].toBool(), true);
    QCOMPARE(busy[1][0].toBool(), false);
    qunsetenv("USAGEBAR_CODEXBAR_CLI");
}

QTEST_GUILESS_MAIN(UsageTest)
#include "usage_test.moc"
