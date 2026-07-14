#include "usagebackend.h"
#include "providers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

UsageBackend::UsageBackend(QObject *parent)
    : QObject(parent), process_(new QProcess(this)), timeout_(new QTimer(this))
{
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this] {
        if (!inFlight_)
            return;
        timedOut_ = true;
        process_->kill();
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { finish(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            failedToStart_ = true;
            finish();
        }
    });
}

QString UsageBackend::findCli()
{
    const auto override = qEnvironmentVariable("USAGEBAR_CODEXBAR_CLI");
    const auto appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        override,
        appDir + "/CodexBarCLI",
        QDir(appDir).absoluteFilePath("../lib/usagebar/CodexBarCLI"),
        QStandardPaths::findExecutable("CodexBarCLI"),
        QStandardPaths::findExecutable("codexbar"),
    };
    for (const auto &candidate : candidates) {
        const QFileInfo file(candidate);
        if (!candidate.isEmpty() && file.isFile() && file.isExecutable())
            return file.canonicalFilePath();
    }
    return {};
}

void UsageBackend::refresh()
{
    if (inFlight_)
        return;
    const auto cli = findCli();
    if (cli.isEmpty()) {
        emit updated(lastGood_.values(), QStringLiteral("CodexBarCLI was not found"));
        return;
    }

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("NO_COLOR", "1");
    environment.insert("TERM", "dumb");
    process_->setProcessEnvironment(environment);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setProgram(cli);
    stage_ = Stage::Usage;
    inFlight_ = true;
    providerIndex_ = 0;
    latest_.clear();
    refreshErrors_.clear();
    emit busyChanged(true);
    startProvider();
}

void UsageBackend::startProvider()
{
    const auto &provider = providerCatalog().at(providerIndex_);
    timedOut_ = false;
    failedToStart_ = false;
    process_->setArguments({"usage", "--provider", provider.id, "--source", provider.source,
                            "--status", "--format", "json"});
    timeout_->start(30000);
    process_->start();
}

void UsageBackend::startCost()
{
    stage_ = Stage::Cost;
    timedOut_ = false;
    failedToStart_ = false;
    process_->setArguments({"cost", "--provider", "both", "--days", "90", "--format", "json"});
    timeout_->start(40000);
    process_->start();
}

void UsageBackend::finish()
{
    if (!inFlight_)
        return;
    timeout_->stop();
    const auto output = process_->readAllStandardOutput();
    const auto stderrText = QString::fromUtf8(process_->readAllStandardError()).trimmed();

    if (stage_ == Stage::Cost) {
        QString costError;
        const auto costs = parseCosts(output, &costError);
        for (auto &provider : pendingProviders_) {
            if (costs.contains(provider.id))
                provider.cost = costs.value(provider.id);
        }
        inFlight_ = false;
        emit updated(pendingProviders_, pendingMessage_);
        emit busyChanged(false);
        return;
    }

    finishUsageProvider(output, stderrText);
    if (failedToStart_) {
        finishRefresh();
        return;
    }
    ++providerIndex_;
    if (providerIndex_ < providerCatalog().size()) {
        startProvider();
        return;
    }
    finishRefresh();
}

void UsageBackend::finishUsageProvider(const QByteArray &output, const QString &stderrText)
{
    const auto &info = providerCatalog().at(providerIndex_);
    QString parseError;
    const auto providers = parseUsage(output, &parseError);
    for (const auto &provider : providers) {
        latest_.insert(provider.id, provider);
        if (provider.error.isEmpty())
            lastGood_.insert(provider.id, provider);
        else
            refreshErrors_.append(provider.id);
    }
    if (latest_.contains(info.id))
        return;

    QString message;
    if (timedOut_)
        message = QStringLiteral("Refresh timed out");
    else if (failedToStart_)
        message = QStringLiteral("CodexBarCLI could not be started");
    else
        message = !stderrText.isEmpty() ? stderrText : parseError;
    if (message.isEmpty())
        message = QStringLiteral("No usage data was returned");
    ProviderUsage unavailable;
    unavailable.id = info.id;
    unavailable.name = info.name;
    unavailable.error = message;
    latest_.insert(info.id, unavailable);
    refreshErrors_.append(info.id);
}

void UsageBackend::finishRefresh()
{
    pendingProviders_.clear();
    for (const auto &info : providerCatalog()) {
        if (latest_.contains(info.id) && !latest_.value(info.id).error.isEmpty()
            && lastGood_.contains(info.id)) {
            auto stale = lastGood_.value(info.id);
            stale.error = latest_.value(info.id).error;
            pendingProviders_.append(stale);
        } else if (latest_.contains(info.id)) {
            pendingProviders_.append(latest_.value(info.id));
        } else if (lastGood_.contains(info.id)) {
            pendingProviders_.append(lastGood_.value(info.id));
        }
    }
    pendingMessage_ = refreshErrors_.isEmpty()
        ? QString()
        : QStringLiteral("%1 provider(s) could not be refreshed").arg(refreshErrors_.size());
    if (!failedToStart_) {
        startCost();
        return;
    }
    inFlight_ = false;
    emit updated(pendingProviders_, pendingMessage_);
    emit busyChanged(false);
}
