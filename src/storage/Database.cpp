#include "vicvpn/storage/Database.h"
#include "vicvpn/util/AppPaths.h"
#include "vicvpn/util/StringUtil.h"
#include <QJsonDocument>
#include <sqlite3.h>

namespace vicvpn {

Database::Database(QObject* parent) : QObject(parent) {}

bool Database::open() {
    if (db_)
        return true;
    if (sqlite3_open(dbPath().toUtf8().constData(), reinterpret_cast<sqlite3**>(&db_)) != SQLITE_OK)
        return false;
    return ensureSchema();
}

QString Database::dbPath() const {
    return AppPaths::dataDir() + "/vicvpn.db";
}

bool Database::ensureSchema() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS servers ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT, remark TEXT, protocol TEXT, core TEXT,"
        "raw_uri TEXT, xray_outbound TEXT, hy2_config TEXT,"
        "passthrough INTEGER, passthrough_config TEXT,"
        "subscription_url TEXT, country_code TEXT, latency_ms INTEGER, updated_at TEXT);"
        "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);";
    char* err = nullptr;
    const int rc = sqlite3_exec(static_cast<sqlite3*>(db_), sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return false;
    }
    sqlite3_exec(static_cast<sqlite3*>(db_),
        "ALTER TABLE servers ADD COLUMN country_code TEXT DEFAULT ''",
        nullptr, nullptr, nullptr);
    return true;
}

ServerProfile Database::rowToProfile(void* stmt) const {
    auto* s = static_cast<sqlite3_stmt*>(stmt);
    ServerProfile p;
    p.id = sqlite3_column_int64(s, 0);
    p.name = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 1)));
    p.remark = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 2)));
    const QString proto = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 3)));
    if (proto == "vless") p.protocol = Protocol::Vless;
    else if (proto == "vmess") p.protocol = Protocol::Vmess;
    else if (proto == "ss") p.protocol = Protocol::Shadowsocks;
    else if (proto == "trojan") p.protocol = Protocol::Trojan;
    else if (proto == "socks") p.protocol = Protocol::Socks;
    else if (proto == "hy2") p.protocol = Protocol::Hysteria2;
    const QString core = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 4)));
    p.core = core == "hysteria2" ? CoreType::Hysteria2 : CoreType::Xray;
    p.rawUri = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 5)));
    const auto xo = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 6)));
    if (!xo.isEmpty()) p.xrayOutbound = nlohmann::json::parse(xo.toStdString());
    const auto hc = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 7)));
    if (!hc.isEmpty()) p.hy2Config = nlohmann::json::parse(hc.toStdString());
    p.passthroughJson = sqlite3_column_int(s, 8) != 0;
    const auto pc = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 9)));
    if (!pc.isEmpty()) p.passthroughConfig = nlohmann::json::parse(pc.toStdString());
    p.subscriptionUrl = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, 10)));
    const auto cc = sqlite3_column_text(s, 11);
    if (cc) p.countryCode = QString::fromUtf8(reinterpret_cast<const char*>(cc));
    p.latencyMs = sqlite3_column_int(s, 12);
  return p;
}

QVector<ServerProfile> Database::allServers() const {
    QVector<ServerProfile> out;
    if (!db_) return out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_),
        "SELECT id,name,remark,protocol,core,raw_uri,xray_outbound,hy2_config,"
        "passthrough,passthrough_config,subscription_url,country_code,latency_ms,updated_at FROM servers ORDER BY id",
        -1, &stmt, nullptr) != SQLITE_OK)
        return out;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        out.push_back(rowToProfile(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::optional<ServerProfile> Database::serverById(qint64 id) const {
    if (!db_) return std::nullopt;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_),
        "SELECT id,name,remark,protocol,core,raw_uri,xray_outbound,hy2_config,"
        "passthrough,passthrough_config,subscription_url,country_code,latency_ms,updated_at FROM servers WHERE id=?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_int64(stmt, 1, id);
    std::optional<ServerProfile> result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = rowToProfile(stmt);
    sqlite3_finalize(stmt);
    return result;
}

static QString protoToStr(Protocol p) {
    switch (p) {
    case Protocol::Vless: return "vless";
    case Protocol::Vmess: return "vmess";
    case Protocol::Shadowsocks: return "ss";
    case Protocol::Trojan: return "trojan";
    case Protocol::Socks: return "socks";
    case Protocol::Hysteria2: return "hy2";
    default: return "unknown";
    }
}

qint64 Database::insertServer(const ServerProfile& s) {
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO servers(name,remark,protocol,core,raw_uri,xray_outbound,hy2_config,"
                      "passthrough,passthrough_config,subscription_url,country_code,latency_ms,updated_at) "
                      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,datetime('now'))";
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;
    const auto xo = QString::fromStdString(s.xrayOutbound.dump());
    const auto hc = QString::fromStdString(s.hy2Config.dump());
    const auto pc = QString::fromStdString(s.passthroughConfig.dump());
    sqlite3_bind_text(stmt, 1, s.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, s.remark.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, protoToStr(s.protocol).toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, s.core == CoreType::Hysteria2 ? "hysteria2" : "xray", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, s.rawUri.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, xo.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, hc.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, s.passthroughJson ? 1 : 0);
    sqlite3_bind_text(stmt, 9, pc.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, s.subscriptionUrl.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, s.countryCode.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 12, s.latencyMs);
  const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? sqlite3_last_insert_rowid(static_cast<sqlite3*>(db_)) : 0;
}

bool Database::updateServer(const ServerProfile& s) {
    if (!db_ || s.id <= 0) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE servers SET name=?,remark=?,protocol=?,core=?,raw_uri=?,xray_outbound=?,"
                      "hy2_config=?,passthrough=?,passthrough_config=?,subscription_url=?,country_code=?,"
                      "latency_ms=?,updated_at=datetime('now') WHERE id=?";
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    const auto xo = QString::fromStdString(s.xrayOutbound.dump());
    const auto hc = QString::fromStdString(s.hy2Config.dump());
    const auto pc = QString::fromStdString(s.passthroughConfig.dump());
    sqlite3_bind_text(stmt, 1, s.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, s.remark.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, protoToStr(s.protocol).toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, s.core == CoreType::Hysteria2 ? "hysteria2" : "xray", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, s.rawUri.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, xo.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, hc.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, s.passthroughJson ? 1 : 0);
    sqlite3_bind_text(stmt, 9, pc.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, s.subscriptionUrl.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, s.countryCode.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 12, s.latencyMs);
    sqlite3_bind_int64(stmt, 13, s.id);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::deleteServer(qint64 id) {
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), "DELETE FROM servers WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, id);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

void Database::setLastServerId(qint64 id) {
    if (!db_) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_),
        "INSERT INTO meta(key,value) VALUES('last_server_id',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        -1, &stmt, nullptr) != SQLITE_OK)
        return;
    sqlite3_bind_text(stmt, 1, QString::number(id).toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

qint64 Database::lastServerId() const {
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), "SELECT value FROM meta WHERE key='last_server_id'", -1, &stmt, nullptr) != SQLITE_OK)
        return 0;
    qint64 id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        id = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))).toLongLong();
    sqlite3_finalize(stmt);
    return id;
}

} // namespace vicvpn
