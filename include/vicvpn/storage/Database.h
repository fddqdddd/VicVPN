#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <QObject>
#include <QVector>
#include <optional>

namespace vicvpn {

class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject* parent = nullptr);
    bool open();
    QVector<ServerProfile> allServers() const;
    std::optional<ServerProfile> serverById(qint64 id) const;
    qint64 insertServer(const ServerProfile& s);
    bool updateServer(const ServerProfile& s);
    bool deleteServer(qint64 id);
    void setLastServerId(qint64 id);
    qint64 lastServerId() const;

private:
    QString dbPath() const;
    bool ensureSchema();
    ServerProfile rowToProfile(void* stmt) const;

    void* db_ = nullptr;
};

} // namespace vicvpn
