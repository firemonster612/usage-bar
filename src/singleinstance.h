#pragma once

#include <QObject>

class QLocalServer;

class SingleInstance final : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QString name, QObject *parent = nullptr);
    bool start(bool requestShow);

signals:
    void showRequested();

private:
    bool notifyExisting(bool requestShow);
    QString name_;
    QLocalServer *server_;
};
