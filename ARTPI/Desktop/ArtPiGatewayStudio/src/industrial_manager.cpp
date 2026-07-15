#include "industrial_manager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTextStream>
#include <QUrl>

namespace
{
constexpr qint64 sampleIntervalMs = 5000;
constexpr qint64 historyRetentionMs = 7LL * 24LL * 60LL * 60LL * 1000LL;
constexpr qint64 pruneIntervalMs = 60LL * 60LL * 1000LL;

QString isoTime(qint64 timestampMs)
{
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(Qt::ISODateWithMs);
}

QVariantMap queryRow(const QSqlQuery &query)
{
    QVariantMap row;
    const QSqlRecord record = query.record();
    for (int index = 0; index < record.count(); ++index)
    {
        row.insert(record.fieldName(index), query.value(index));
    }
    return row;
}

int registerClassIndex(const QString &name)
{
    if (name == QStringLiteral("coil"))
    {
        return 0;
    }
    if (name == QStringLiteral("discrete"))
    {
        return 1;
    }
    if (name == QStringLiteral("holding"))
    {
        return 2;
    }
    if (name == QStringLiteral("input"))
    {
        return 3;
    }
    return -1;
}
}

IndustrialManager::IndustrialManager(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("artpi_industrial_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16))
{
    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDirectory);
    m_databasePath = QDir(dataDirectory).filePath(QStringLiteral("gateway_studio.db"));

    connect(&m_outletSocket, &QTcpSocket::connected, this, [this]() {
        setOutletState(QStringLiteral("connecting"));
        sendMqttConnect();
    });
    connect(&m_outletSocket, &QTcpSocket::readyRead, this, [this]() {
        const QByteArray response = m_outletSocket.readAll();
        if (response.size() >= 4 && static_cast<quint8>(response.at(0)) == 0x20U &&
            static_cast<quint8>(response.at(3)) == 0U)
        {
            m_mqttReady = true;
            setOutletState(QStringLiteral("online"));
            recordLog(QStringLiteral("success"), QStringLiteral("mqtt"),
                      tr("MQTT 出口已连接"));
        }
    });
    connect(&m_outletSocket, &QTcpSocket::disconnected, this, [this]() {
        m_mqttReady = false;
        if (m_outlet.value(QStringLiteral("enabled")).toBool())
        {
            setOutletState(QStringLiteral("offline"));
        }
    });
    connect(&m_outletSocket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        m_mqttReady = false;
        setOutletState(QStringLiteral("error"));
        recordLog(QStringLiteral("error"), QStringLiteral("mqtt"),
                  m_outletSocket.errorString());
    });

    m_outletTimer.setSingleShot(false);
    connect(&m_outletTimer, &QTimer::timeout,
            this, &IndustrialManager::publishCurrentSnapshot);

    if (!openDatabase() || !createSchema())
    {
        emit notification(tr("工业数据库初始化失败：%1")
                              .arg(m_database.lastError().text()), false);
        return;
    }

    loadSecurityState();
    loadOutletSettings();
    refreshTags();
    refreshAlarms();
    refreshAudit();
    refreshConfigVersions();
    refreshGateways();
    refreshUsers();
    refreshPersistentLogs();
    refreshMaintenance();
}

IndustrialManager::~IndustrialManager()
{
    m_outletTimer.stop();
    m_outletSocket.abort();
    if (m_database.isValid())
    {
        m_database.close();
    }
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString IndustrialManager::databasePath() const { return m_databasePath; }
QVariantList IndustrialManager::tags() const { return m_tags; }
QVariantList IndustrialManager::alarms() const { return m_alarms; }
QVariantList IndustrialManager::history() const { return m_history; }
QVariantList IndustrialManager::audit() const { return m_audit; }
QVariantList IndustrialManager::configVersions() const { return m_configVersions; }
QVariantList IndustrialManager::gateways() const { return m_gateways; }
QVariantList IndustrialManager::users() const { return m_users; }
QVariantList IndustrialManager::persistentLogs() const { return m_persistentLogs; }
QVariantMap IndustrialManager::maintenance() const { return m_maintenance; }
QVariantMap IndustrialManager::outlet() const { return m_outlet; }
QString IndustrialManager::outletState() const { return m_outletState; }
QString IndustrialManager::currentUser() const { return m_currentUser; }
QString IndustrialManager::currentRole() const { return m_currentRole; }
bool IndustrialManager::authenticated() const { return m_authenticated; }
bool IndustrialManager::securityInitialized() const { return m_securityInitialized; }

int IndustrialManager::activeAlarmCount() const
{
    int count = 0;
    for (const QVariant &entry : m_alarms)
    {
        if (entry.toMap().value(QStringLiteral("active")).toBool())
        {
            ++count;
        }
    }
    return count;
}

bool IndustrialManager::canOperate() const
{
    return !m_securityInitialized ||
           (m_authenticated && m_currentRole != QStringLiteral("viewer"));
}

bool IndustrialManager::canConfigure() const
{
    return !m_securityInitialized ||
           (m_authenticated && (m_currentRole == QStringLiteral("administrator") ||
                                m_currentRole == QStringLiteral("engineer")));
}

bool IndustrialManager::openDatabase()
{
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open())
    {
        return false;
    }

    QSqlQuery query(m_database);
    query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    query.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    query.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
    return true;
}

bool IndustrialManager::createSchema()
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS tags ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
                       "device_index INTEGER NOT NULL, register_class TEXT NOT NULL, "
                       "value_index INTEGER NOT NULL, scale REAL NOT NULL DEFAULT 1, "
                       "value_offset REAL NOT NULL DEFAULT 0, unit TEXT NOT NULL DEFAULT '', "
                       "low_enabled INTEGER NOT NULL DEFAULT 0, low_limit REAL NOT NULL DEFAULT 0, "
                       "high_enabled INTEGER NOT NULL DEFAULT 0, high_limit REAL NOT NULL DEFAULT 0, "
                       "enabled INTEGER NOT NULL DEFAULT 1, UNIQUE(device_index, register_class, value_index))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS samples ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, tag_id INTEGER NOT NULL, "
                       "timestamp_ms INTEGER NOT NULL, value REAL NOT NULL, quality TEXT NOT NULL, "
                       "FOREIGN KEY(tag_id) REFERENCES tags(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_tag_time "
                       "ON samples(tag_id, timestamp_ms)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS alarms ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, tag_id INTEGER NOT NULL, "
                       "raised_ms INTEGER NOT NULL, cleared_ms INTEGER, acknowledged_ms INTEGER, "
                       "active INTEGER NOT NULL DEFAULT 1, acknowledged INTEGER NOT NULL DEFAULT 0, "
                       "severity TEXT NOT NULL, message TEXT NOT NULL, value REAL NOT NULL, "
                       "FOREIGN KEY(tag_id) REFERENCES tags(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alarms_state ON alarms(active, raised_ms)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS audit ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp_ms INTEGER NOT NULL, "
                       "username TEXT NOT NULL, action TEXT NOT NULL, target TEXT NOT NULL, "
                       "detail TEXT NOT NULL, success INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS config_versions ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, created_ms INTEGER NOT NULL, "
                       "label TEXT NOT NULL, endpoint TEXT NOT NULL, json TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS gateways ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, endpoint TEXT NOT NULL UNIQUE, "
                       "notes TEXT NOT NULL DEFAULT '', last_seen_ms INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS app_logs ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp_ms INTEGER NOT NULL, "
                       "level TEXT NOT NULL, source TEXT NOT NULL, message TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_logs_time ON app_logs(timestamp_ms)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS users ("
                       "username TEXT PRIMARY KEY, password_hash TEXT NOT NULL, salt TEXT NOT NULL, "
                       "role TEXT NOT NULL, enabled INTEGER NOT NULL DEFAULT 1)"),
        QStringLiteral("INSERT OR IGNORE INTO gateways(name, endpoint, notes) "
                       "VALUES('ART-Pi H750', 'http://192.168.1.20', '板载 NetX Duo 网关')")
    };

    QSqlQuery query(m_database);
    for (const QString &statement : statements)
    {
        if (!query.exec(statement))
        {
            return false;
        }
    }
    return true;
}

void IndustrialManager::loadSecurityState()
{
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT COUNT(*) FROM users WHERE enabled=1"));
    m_securityInitialized = query.next() && query.value(0).toInt() > 0;
    if (m_securityInitialized)
    {
        m_currentUser.clear();
        m_currentRole = QStringLiteral("viewer");
        m_authenticated = false;
    }
    emit securityChanged();
}

void IndustrialManager::loadOutletSettings()
{
    QSettings settings;
    m_outlet = {
        {QStringLiteral("enabled"), settings.value(QStringLiteral("outlet/enabled"), false)},
        {QStringLiteral("type"), QStringLiteral("mqtt")},
        {QStringLiteral("host"), settings.value(QStringLiteral("outlet/host"), QStringLiteral("127.0.0.1"))},
        {QStringLiteral("port"), settings.value(QStringLiteral("outlet/port"), 1883)},
        {QStringLiteral("topic"), settings.value(QStringLiteral("outlet/topic"), QStringLiteral("artpi/status"))},
        {QStringLiteral("clientId"), settings.value(QStringLiteral("outlet/clientId"), QStringLiteral("artpi-gateway-studio"))},
        {QStringLiteral("username"), settings.value(QStringLiteral("outlet/username"), QString())},
        {QStringLiteral("password"), QString()},
        {QStringLiteral("intervalSeconds"), settings.value(QStringLiteral("outlet/intervalSeconds"), 10)}
    };
    const int interval = qBound(5, m_outlet.value(QStringLiteral("intervalSeconds")).toInt(), 3600);
    m_outletTimer.setInterval(interval * 1000);
    if (m_outlet.value(QStringLiteral("enabled")).toBool())
    {
        m_outletTimer.start();
        ensureOutletConnected();
    }
    emit outletChanged();
}

void IndustrialManager::refreshTags()
{
    QVariantList rows;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT id,name,device_index,register_class,value_index,scale,"
                              "value_offset,unit,low_enabled,low_limit,high_enabled,high_limit,enabled "
                              "FROM tags ORDER BY device_index,register_class,value_index"));
    while (query.next())
    {
        QVariantMap row = queryRow(query);
        const QString key = row.value(QStringLiteral("id")).toString();
        row.insert(QStringLiteral("value"), m_latestValues.value(key));
        row.insert(QStringLiteral("quality"), m_latestQuality.value(key, QStringLiteral("unknown")));
        rows.append(row);
    }
    m_tags = rows;
    emit tagsChanged();
}

void IndustrialManager::refreshAlarms()
{
    QVariantList rows;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT alarms.id,alarms.tag_id,tags.name AS tag_name,"
                              "alarms.raised_ms,alarms.cleared_ms,alarms.acknowledged_ms,"
                              "alarms.active,alarms.acknowledged,alarms.severity,alarms.message,alarms.value "
                              "FROM alarms JOIN tags ON tags.id=alarms.tag_id "
                              "ORDER BY alarms.active DESC, alarms.raised_ms DESC LIMIT 300"));
    while (query.next())
    {
        QVariantMap row = queryRow(query);
        row.insert(QStringLiteral("raised_at"), isoTime(row.value(QStringLiteral("raised_ms")).toLongLong()));
        if (!row.value(QStringLiteral("cleared_ms")).isNull())
        {
            row.insert(QStringLiteral("cleared_at"), isoTime(row.value(QStringLiteral("cleared_ms")).toLongLong()));
        }
        rows.append(row);
    }
    m_alarms = rows;
    emit alarmsChanged();
}

void IndustrialManager::refreshAudit()
{
    QVariantList rows;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT id,timestamp_ms,username,action,target,detail,success "
                              "FROM audit ORDER BY timestamp_ms DESC LIMIT 300"));
    while (query.next())
    {
        QVariantMap row = queryRow(query);
        row.insert(QStringLiteral("timestamp"), isoTime(row.value(QStringLiteral("timestamp_ms")).toLongLong()));
        rows.append(row);
    }
    m_audit = rows;
    emit auditChanged();
}

void IndustrialManager::refreshConfigVersions()
{
    QVariantList rows;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT id,created_ms,label,endpoint FROM config_versions "
                              "ORDER BY created_ms DESC LIMIT 100"));
    while (query.next())
    {
        QVariantMap row = queryRow(query);
        row.insert(QStringLiteral("created_at"), isoTime(row.value(QStringLiteral("created_ms")).toLongLong()));
        rows.append(row);
    }
    m_configVersions = rows;
    emit configVersionsChanged();
}

void IndustrialManager::refreshGateways()
{
    QVariantList rows;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT id,name,endpoint,notes,last_seen_ms FROM gateways ORDER BY name"));
    while (query.next())
    {
        QVariantMap row = queryRow(query);
        const qint64 lastSeen = row.value(QStringLiteral("last_seen_ms")).toLongLong();
        row.insert(QStringLiteral("last_seen"), lastSeen > 0 ? isoTime(lastSeen) : QStringLiteral("—"));
        rows.append(row);
    }
    m_gateways = rows;
    emit gatewaysChanged();
}

void IndustrialManager::refreshUsers()
{
    QVariantList rows;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT username,role,enabled FROM users ORDER BY username"));
    while (query.next())
    {
        rows.append(queryRow(query));
    }
    m_users = rows;
    emit usersChanged();
}

void IndustrialManager::refreshPersistentLogs()
{
    QVariantList rows;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("SELECT id,timestamp_ms,level,source,message FROM app_logs "
                              "ORDER BY timestamp_ms DESC LIMIT 500"));
    while (query.next())
    {
        QVariantMap row = queryRow(query);
        row.insert(QStringLiteral("timestamp"), isoTime(row.value(QStringLiteral("timestamp_ms")).toLongLong()));
        rows.append(row);
    }
    m_persistentLogs = rows;
    emit persistentLogsChanged();
}

void IndustrialManager::ingestSnapshot(const QVariantMap &snapshot)
{
    if (snapshot.isEmpty())
    {
        return;
    }

    m_latestSnapshot = snapshot;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    seedDefaultTags(snapshot);

    if (now - m_lastGatewayUpdateMs >= 10000)
    {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("UPDATE gateways SET last_seen_ms=? WHERE endpoint=?"));
        query.addBindValue(now);
        query.addBindValue(QStringLiteral("http://%1").arg(snapshot.value(QStringLiteral("ip")).toString()));
        query.exec();
        m_lastGatewayUpdateMs = now;
    }

    if (m_lastSampleMs == 0 || now - m_lastSampleMs >= sampleIntervalMs)
    {
        storeSamples(snapshot, now);
        m_lastSampleMs = now;
        refreshTags();
        refreshAlarms();
        refreshMaintenance();
    }

    if (m_lastPruneMs == 0 || now - m_lastPruneMs >= pruneIntervalMs)
    {
        pruneDatabase(now);
        m_lastPruneMs = now;
    }
}

void IndustrialManager::seedDefaultTags(const QVariantMap &snapshot)
{
    if (!m_tags.isEmpty())
    {
        return;
    }
    const QVariantList devices = snapshot.value(QStringLiteral("rs485")).toMap()
                                     .value(QStringLiteral("devices")).toList();
    if (devices.isEmpty())
    {
        return;
    }

    for (int index = 0; index < devices.size(); ++index)
    {
        QVariantMap definition{
            {QStringLiteral("name"), tr("设备 %1 输入值").arg(index + 1)},
            {QStringLiteral("device_index"), index},
            {QStringLiteral("register_class"), QStringLiteral("input")},
            {QStringLiteral("value_index"), 0},
            {QStringLiteral("scale"), 1.0},
            {QStringLiteral("value_offset"), 0.0},
            {QStringLiteral("unit"), QStringLiteral("raw")},
            {QStringLiteral("low_enabled"), false},
            {QStringLiteral("low_limit"), 0.0},
            {QStringLiteral("high_enabled"), false},
            {QStringLiteral("high_limit"), 0.0},
            {QStringLiteral("enabled"), true}
        };
        addTag(definition);
    }
    recordLog(QStringLiteral("success"), QStringLiteral("history"),
              tr("已按在线从机自动建立 %1 个默认点位").arg(devices.size()));
}

void IndustrialManager::storeSamples(const QVariantMap &snapshot, qint64 timestampMs)
{
    const QVariantList devices = snapshot.value(QStringLiteral("rs485")).toMap()
                                     .value(QStringLiteral("devices")).toList();
    if (devices.isEmpty())
    {
        return;
    }

    QSqlQuery tagQuery(m_database);
    tagQuery.exec(QStringLiteral("SELECT id,name,device_index,register_class,value_index,scale,value_offset,"
                                 "low_enabled,low_limit,high_enabled,high_limit FROM tags WHERE enabled=1"));
    m_database.transaction();
    while (tagQuery.next())
    {
        const int tagId = tagQuery.value(0).toInt();
        const QString tagName = tagQuery.value(1).toString();
        const int deviceIndex = tagQuery.value(2).toInt();
        const int classIndex = registerClassIndex(tagQuery.value(3).toString());
        const int valueIndex = tagQuery.value(4).toInt();
        QString quality = QStringLiteral("bad");
        double value = 0.0;

        if (deviceIndex >= 0 && deviceIndex < devices.size() && classIndex >= 0)
        {
            const QVariantMap device = devices.at(deviceIndex).toMap();
            const QVariantList classes = device.value(QStringLiteral("values")).toList();
            if (classIndex < classes.size())
            {
                const QVariantList values = classes.at(classIndex).toList();
                if (valueIndex >= 0 && valueIndex < values.size())
                {
                    value = values.at(valueIndex).toDouble() * tagQuery.value(5).toDouble() +
                            tagQuery.value(6).toDouble();
                    quality = device.value(QStringLiteral("state")).toString() == QStringLiteral("online")
                                  ? QStringLiteral("good") : QStringLiteral("uncertain");
                }
            }
        }

        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral("INSERT INTO samples(tag_id,timestamp_ms,value,quality) VALUES(?,?,?,?)"));
        insert.addBindValue(tagId);
        insert.addBindValue(timestampMs);
        insert.addBindValue(value);
        insert.addBindValue(quality);
        insert.exec();

        const QString key = QString::number(tagId);
        m_latestValues.insert(key, value);
        m_latestQuality.insert(key, quality);
        if (quality == QStringLiteral("good"))
        {
            evaluateAlarm(tagId, tagName, value,
                          tagQuery.value(7).toBool(), tagQuery.value(8).toDouble(),
                          tagQuery.value(9).toBool(), tagQuery.value(10).toDouble(),
                          timestampMs);
        }
    }
    m_database.commit();
}

void IndustrialManager::evaluateAlarm(int tagId,
                                      const QString &tagName,
                                      double value,
                                      bool lowEnabled,
                                      double lowLimit,
                                      bool highEnabled,
                                      double highLimit,
                                      qint64 timestampMs)
{
    QString message;
    QString severity;
    if (highEnabled && value > highLimit)
    {
        severity = QStringLiteral("high");
        message = tr("%1 高于上限 %2（当前 %3）").arg(tagName).arg(highLimit).arg(value);
    }
    else if (lowEnabled && value < lowLimit)
    {
        severity = QStringLiteral("medium");
        message = tr("%1 低于下限 %2（当前 %3）").arg(tagName).arg(lowLimit).arg(value);
    }

    QSqlQuery activeQuery(m_database);
    activeQuery.prepare(QStringLiteral("SELECT id FROM alarms WHERE tag_id=? AND active=1 LIMIT 1"));
    activeQuery.addBindValue(tagId);
    activeQuery.exec();
    const bool hasActiveAlarm = activeQuery.next();
    const int activeId = hasActiveAlarm ? activeQuery.value(0).toInt() : 0;

    if (!message.isEmpty() && !hasActiveAlarm)
    {
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral("INSERT INTO alarms(tag_id,raised_ms,severity,message,value) "
                                      "VALUES(?,?,?,?,?)"));
        insert.addBindValue(tagId);
        insert.addBindValue(timestampMs);
        insert.addBindValue(severity);
        insert.addBindValue(message);
        insert.addBindValue(value);
        insert.exec();
        recordLog(QStringLiteral("error"), QStringLiteral("alarm"), message);
    }
    else if (message.isEmpty() && hasActiveAlarm)
    {
        QSqlQuery clear(m_database);
        clear.prepare(QStringLiteral("UPDATE alarms SET active=0,cleared_ms=? WHERE id=?"));
        clear.addBindValue(timestampMs);
        clear.addBindValue(activeId);
        clear.exec();
        recordLog(QStringLiteral("success"), QStringLiteral("alarm"),
                  tr("%1 已恢复正常").arg(tagName));
    }
}

QVariantMap IndustrialManager::normalizedTag(const QVariantMap &definition) const
{
    QVariantMap tag = definition;
    tag.insert(QStringLiteral("name"), definition.value(QStringLiteral("name")).toString().trimmed());
    tag.insert(QStringLiteral("device_index"), definition.value(QStringLiteral("device_index")).toInt());
    tag.insert(QStringLiteral("register_class"), definition.value(QStringLiteral("register_class")).toString().toLower());
    tag.insert(QStringLiteral("value_index"), definition.value(QStringLiteral("value_index")).toInt());
    tag.insert(QStringLiteral("scale"), definition.value(QStringLiteral("scale"), 1.0).toDouble());
    tag.insert(QStringLiteral("value_offset"), definition.value(QStringLiteral("value_offset"), 0.0).toDouble());
    tag.insert(QStringLiteral("unit"), definition.value(QStringLiteral("unit")).toString().trimmed());
    tag.insert(QStringLiteral("low_enabled"), definition.value(QStringLiteral("low_enabled"), false).toBool());
    tag.insert(QStringLiteral("low_limit"), definition.value(QStringLiteral("low_limit"), 0.0).toDouble());
    tag.insert(QStringLiteral("high_enabled"), definition.value(QStringLiteral("high_enabled"), false).toBool());
    tag.insert(QStringLiteral("high_limit"), definition.value(QStringLiteral("high_limit"), 0.0).toDouble());
    tag.insert(QStringLiteral("enabled"), definition.value(QStringLiteral("enabled"), true).toBool());
    return tag;
}

bool IndustrialManager::validateTag(const QVariantMap &definition, QString *error) const
{
    const QVariantMap tag = normalizedTag(definition);
    if (tag.value(QStringLiteral("name")).toString().isEmpty())
    {
        *error = tr("点位名称不能为空");
        return false;
    }
    if (tag.value(QStringLiteral("device_index")).toInt() < 0 ||
        tag.value(QStringLiteral("device_index")).toInt() > 9)
    {
        *error = tr("从机索引必须为 0~9");
        return false;
    }
    if (registerClassIndex(tag.value(QStringLiteral("register_class")).toString()) < 0)
    {
        *error = tr("寄存器类型无效");
        return false;
    }
    if (tag.value(QStringLiteral("value_index")).toInt() < 0 ||
        tag.value(QStringLiteral("value_index")).toInt() > 15)
    {
        *error = tr("值索引必须为 0~15");
        return false;
    }
    if (tag.value(QStringLiteral("low_enabled")).toBool() &&
        tag.value(QStringLiteral("high_enabled")).toBool() &&
        tag.value(QStringLiteral("low_limit")).toDouble() >=
            tag.value(QStringLiteral("high_limit")).toDouble())
    {
        *error = tr("报警下限必须小于上限");
        return false;
    }
    return true;
}

int IndustrialManager::addTag(const QVariantMap &definition)
{
    if (!canConfigure())
    {
        emit notification(tr("当前用户无权修改点表"), false);
        return -1;
    }
    QString error;
    if (!validateTag(definition, &error))
    {
        emit notification(error, false);
        return -1;
    }
    const QVariantMap tag = normalizedTag(definition);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO tags(name,device_index,register_class,value_index,scale,"
                                 "value_offset,unit,low_enabled,low_limit,high_enabled,high_limit,enabled) "
                                 "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
    for (const QString &key : {QStringLiteral("name"), QStringLiteral("device_index"),
                               QStringLiteral("register_class"), QStringLiteral("value_index"),
                               QStringLiteral("scale"), QStringLiteral("value_offset"),
                               QStringLiteral("unit"), QStringLiteral("low_enabled"),
                               QStringLiteral("low_limit"), QStringLiteral("high_enabled"),
                               QStringLiteral("high_limit"), QStringLiteral("enabled")})
    {
        query.addBindValue(tag.value(key));
    }
    if (!query.exec())
    {
        emit notification(tr("新增点位失败：%1").arg(query.lastError().text()), false);
        return -1;
    }
    const int id = query.lastInsertId().toInt();
    recordAudit(QStringLiteral("tag.create"), QString::number(id),
                tag.value(QStringLiteral("name")).toString(), true);
    refreshTags();
    emit notification(tr("点位已创建"), true);
    return id;
}

bool IndustrialManager::updateTag(int id, const QVariantMap &definition)
{
    if (!canConfigure())
    {
        emit notification(tr("当前用户无权修改点表"), false);
        return false;
    }
    QString error;
    if (!validateTag(definition, &error))
    {
        emit notification(error, false);
        return false;
    }
    const QVariantMap tag = normalizedTag(definition);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE tags SET name=?,device_index=?,register_class=?,value_index=?,"
                                 "scale=?,value_offset=?,unit=?,low_enabled=?,low_limit=?,high_enabled=?,"
                                 "high_limit=?,enabled=? WHERE id=?"));
    for (const QString &key : {QStringLiteral("name"), QStringLiteral("device_index"),
                               QStringLiteral("register_class"), QStringLiteral("value_index"),
                               QStringLiteral("scale"), QStringLiteral("value_offset"),
                               QStringLiteral("unit"), QStringLiteral("low_enabled"),
                               QStringLiteral("low_limit"), QStringLiteral("high_enabled"),
                               QStringLiteral("high_limit"), QStringLiteral("enabled")})
    {
        query.addBindValue(tag.value(key));
    }
    query.addBindValue(id);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    recordAudit(QStringLiteral("tag.update"), QString::number(id),
                tag.value(QStringLiteral("name")).toString(), ok);
    refreshTags();
    emit notification(ok ? tr("点位已更新") : tr("点位更新失败"), ok);
    return ok;
}

bool IndustrialManager::removeTag(int id)
{
    if (!canConfigure())
    {
        emit notification(tr("当前用户无权删除点位"), false);
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM tags WHERE id=?"));
    query.addBindValue(id);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    recordAudit(QStringLiteral("tag.delete"), QString::number(id), QString(), ok);
    refreshTags();
    refreshAlarms();
    return ok;
}

void IndustrialManager::selectHistory(int tagId, int hours)
{
    hours = qBound(1, hours, 24 * 30);
    const qint64 from = QDateTime::currentMSecsSinceEpoch() -
                        static_cast<qint64>(hours) * 60LL * 60LL * 1000LL;
    QVariantList rows;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT timestamp_ms,value,quality FROM samples "
                                 "WHERE tag_id=? AND timestamp_ms>=? ORDER BY timestamp_ms LIMIT 5000"));
    query.addBindValue(tagId);
    query.addBindValue(from);
    query.exec();
    while (query.next())
    {
        rows.append(QVariantMap{{QStringLiteral("timestamp_ms"), query.value(0)},
                                {QStringLiteral("time"), isoTime(query.value(0).toLongLong())},
                                {QStringLiteral("value"), query.value(1)},
                                {QStringLiteral("quality"), query.value(2)}});
    }
    m_history = rows;
    emit historyChanged();
}

bool IndustrialManager::acknowledgeAlarm(int alarmId)
{
    if (!canOperate())
    {
        emit notification(tr("当前用户无权确认报警"), false);
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE alarms SET acknowledged=1,acknowledged_ms=? WHERE id=?"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(alarmId);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    recordAudit(QStringLiteral("alarm.ack"), QString::number(alarmId), QString(), ok);
    refreshAlarms();
    return ok;
}

int IndustrialManager::acknowledgeAllAlarms()
{
    if (!canOperate())
    {
        emit notification(tr("当前用户无权确认报警"), false);
        return 0;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE alarms SET acknowledged=1,acknowledged_ms=? "
                                 "WHERE acknowledged=0"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.exec();
    const int count = query.numRowsAffected();
    recordAudit(QStringLiteral("alarm.ack_all"), QStringLiteral("alarms"),
                QString::number(count), true);
    refreshAlarms();
    return count;
}

void IndustrialManager::recordAudit(const QString &action,
                                    const QString &target,
                                    const QString &detail,
                                    bool success)
{
    if (!m_database.isOpen())
    {
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO audit(timestamp_ms,username,action,target,detail,success) "
                                 "VALUES(?,?,?,?,?,?)"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(m_currentUser.isEmpty() ? QStringLiteral("anonymous") : m_currentUser);
    query.addBindValue(action);
    query.addBindValue(target);
    query.addBindValue(detail);
    query.addBindValue(success);
    query.exec();
    refreshAudit();
}

void IndustrialManager::recordLog(const QString &level,
                                  const QString &source,
                                  const QString &message)
{
    if (!m_database.isOpen() || message.trimmed().isEmpty())
    {
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO app_logs(timestamp_ms,level,source,message) VALUES(?,?,?,?)"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(level);
    query.addBindValue(source);
    query.addBindValue(message.left(1000));
    query.exec();
    refreshPersistentLogs();
}

void IndustrialManager::clearPersistentLogs()
{
    if (!canConfigure())
    {
        emit notification(tr("当前用户无权清理日志"), false);
        return;
    }
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("DELETE FROM app_logs"));
    recordAudit(QStringLiteral("logs.clear"), QStringLiteral("app_logs"), QString(), true);
    refreshPersistentLogs();
}

int IndustrialManager::saveConfigurationVersion(const QString &label,
                                                 const QString &endpoint,
                                                 const QVariantMap &configuration)
{
    if (!canConfigure() || configuration.isEmpty())
    {
        emit notification(tr("没有可保存的配置或权限不足"), false);
        return -1;
    }
    const QByteArray json = QJsonDocument::fromVariant(configuration).toJson(QJsonDocument::Compact);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO config_versions(created_ms,label,endpoint,json) VALUES(?,?,?,?)"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(label.trimmed().isEmpty() ? tr("手动快照") : label.trimmed());
    query.addBindValue(endpoint);
    query.addBindValue(QString::fromUtf8(json));
    if (!query.exec())
    {
        emit notification(tr("配置版本保存失败"), false);
        return -1;
    }
    const int id = query.lastInsertId().toInt();
    recordAudit(QStringLiteral("config.snapshot"), endpoint, QString::number(id), true);
    refreshConfigVersions();
    emit notification(tr("配置版本已保存"), true);
    return id;
}

QVariantMap IndustrialManager::restoreConfigurationVersion(int id) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT json FROM config_versions WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
    {
        return {};
    }
    return QJsonDocument::fromJson(query.value(0).toString().toUtf8()).object().toVariantMap();
}

bool IndustrialManager::removeConfigurationVersion(int id)
{
    if (!canConfigure())
    {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM config_versions WHERE id=?"));
    query.addBindValue(id);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    recordAudit(QStringLiteral("config.delete"), QString::number(id), QString(), ok);
    refreshConfigVersions();
    return ok;
}

int IndustrialManager::addGateway(const QString &name,
                                  const QString &endpoint,
                                  const QString &notes)
{
    if (!canConfigure())
    {
        return -1;
    }
    const QUrl url(endpoint.trimmed());
    if (!url.isValid() || url.host().isEmpty())
    {
        emit notification(tr("网关地址无效"), false);
        return -1;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO gateways(name,endpoint,notes) VALUES(?,?,?) "
                                 "ON CONFLICT(endpoint) DO UPDATE SET name=excluded.name,notes=excluded.notes"));
    query.addBindValue(name.trimmed().isEmpty() ? url.host() : name.trimmed());
    query.addBindValue(url.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment));
    query.addBindValue(notes.trimmed());
    if (!query.exec())
    {
        emit notification(tr("网关档案保存失败"), false);
        return -1;
    }
    refreshGateways();
    recordAudit(QStringLiteral("gateway.save"), endpoint, name, true);
    return query.lastInsertId().toInt();
}

bool IndustrialManager::removeGateway(int id)
{
    if (!canConfigure())
    {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM gateways WHERE id=?"));
    query.addBindValue(id);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    refreshGateways();
    recordAudit(QStringLiteral("gateway.delete"), QString::number(id), QString(), ok);
    return ok;
}

void IndustrialManager::activateGateway(int id)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT endpoint FROM gateways WHERE id=?"));
    query.addBindValue(id);
    if (query.exec() && query.next())
    {
        const QString endpoint = query.value(0).toString();
        recordAudit(QStringLiteral("gateway.activate"), endpoint, QString(), true);
        emit gatewayActivationRequested(endpoint);
    }
}

QString IndustrialManager::derivePassword(const QString &password, const QByteArray &salt) const
{
    QByteArray digest = salt + password.toUtf8();
    for (int round = 0; round < 25000; ++round)
    {
        digest = QCryptographicHash::hash(digest + salt, QCryptographicHash::Sha256);
    }
    return QString::fromLatin1(digest.toHex());
}

bool IndustrialManager::initializeSecurity(const QString &password)
{
    if (m_securityInitialized)
    {
        emit notification(tr("本机安全账户已经初始化"), false);
        return false;
    }
    if (password.size() < 8)
    {
        emit notification(tr("管理员密码至少需要 8 个字符"), false);
        return false;
    }
    QByteArray salt(16, Qt::Uninitialized);
    for (char &byte : salt)
    {
        byte = static_cast<char>(QRandomGenerator::global()->generate() & 0xFFU);
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO users(username,password_hash,salt,role,enabled) "
                                 "VALUES('admin',?,?, 'administrator',1)"));
    query.addBindValue(derivePassword(password, salt));
    query.addBindValue(QString::fromLatin1(salt.toHex()));
    if (!query.exec())
    {
        emit notification(tr("安全账户初始化失败"), false);
        return false;
    }
    m_securityInitialized = true;
    m_currentUser = QStringLiteral("admin");
    m_currentRole = QStringLiteral("administrator");
    m_authenticated = true;
    emit securityChanged();
    refreshUsers();
    recordAudit(QStringLiteral("security.initialize"), QStringLiteral("admin"), QString(), true);
    emit notification(tr("管理员账户已启用"), true);
    return true;
}

bool IndustrialManager::login(const QString &username, const QString &password)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT password_hash,salt,role FROM users "
                                 "WHERE username=? AND enabled=1"));
    query.addBindValue(username.trimmed());
    if (!query.exec() || !query.next())
    {
        emit notification(tr("用户名或密码错误"), false);
        return false;
    }
    const QByteArray salt = QByteArray::fromHex(query.value(1).toString().toLatin1());
    if (derivePassword(password, salt) != query.value(0).toString())
    {
        emit notification(tr("用户名或密码错误"), false);
        return false;
    }
    m_currentUser = username.trimmed();
    m_currentRole = query.value(2).toString();
    m_authenticated = true;
    emit securityChanged();
    recordAudit(QStringLiteral("security.login"), m_currentUser, QString(), true);
    return true;
}

void IndustrialManager::logout()
{
    if (!m_securityInitialized)
    {
        return;
    }
    recordAudit(QStringLiteral("security.logout"), m_currentUser, QString(), true);
    m_currentUser.clear();
    m_currentRole = QStringLiteral("viewer");
    m_authenticated = false;
    emit securityChanged();
}

bool IndustrialManager::saveUser(const QString &username,
                                 const QString &password,
                                 const QString &role,
                                 bool enabled)
{
    if (!canConfigure() || (m_securityInitialized && m_currentRole != QStringLiteral("administrator")))
    {
        emit notification(tr("只有管理员可以维护用户"), false);
        return false;
    }
    const QString normalizedName = username.trimmed();
    const QString normalizedRole = role.toLower();
    const QStringList roles{QStringLiteral("viewer"), QStringLiteral("operator"),
                            QStringLiteral("engineer"), QStringLiteral("administrator")};
    if (normalizedName.size() < 3 || password.size() < 8 || !roles.contains(normalizedRole))
    {
        emit notification(tr("用户名至少 3 位、密码至少 8 位，并选择有效角色"), false);
        return false;
    }
    if (normalizedName == QStringLiteral("admin") &&
        (normalizedRole != QStringLiteral("administrator") || !enabled))
    {
        emit notification(tr("内置 admin 必须保持管理员并启用"), false);
        return false;
    }

    QByteArray salt(16, Qt::Uninitialized);
    for (char &byte : salt)
    {
        byte = static_cast<char>(QRandomGenerator::global()->generate() & 0xFFU);
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO users(username,password_hash,salt,role,enabled) VALUES(?,?,?,?,?) "
                                 "ON CONFLICT(username) DO UPDATE SET password_hash=excluded.password_hash,"
                                 "salt=excluded.salt,role=excluded.role,enabled=excluded.enabled"));
    query.addBindValue(normalizedName);
    query.addBindValue(derivePassword(password, salt));
    query.addBindValue(QString::fromLatin1(salt.toHex()));
    query.addBindValue(normalizedRole);
    query.addBindValue(enabled);
    const bool ok = query.exec();
    recordAudit(QStringLiteral("security.user_save"), normalizedName, normalizedRole, ok);
    refreshUsers();
    emit notification(ok ? tr("用户已保存") : tr("用户保存失败"), ok);
    return ok;
}

bool IndustrialManager::removeUser(const QString &username)
{
    if (!m_securityInitialized || !m_authenticated ||
        m_currentRole != QStringLiteral("administrator"))
    {
        emit notification(tr("只有管理员可以删除用户"), false);
        return false;
    }
    const QString normalizedName = username.trimmed();
    if (normalizedName == QStringLiteral("admin") || normalizedName == m_currentUser)
    {
        emit notification(tr("不能删除内置管理员或当前登录用户"), false);
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM users WHERE username=?"));
    query.addBindValue(normalizedName);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    recordAudit(QStringLiteral("security.user_delete"), normalizedName, QString(), ok);
    refreshUsers();
    return ok;
}

QString IndustrialManager::normalizedFilePath(const QString &path) const
{
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : QDir::fromNativeSeparators(path);
}

bool IndustrialManager::backupDatabase(const QString &path)
{
    if (!canConfigure())
    {
        emit notification(tr("当前用户无权备份数据库"), false);
        return false;
    }
    QString destination = normalizedFilePath(path);
    if (destination.isEmpty())
    {
        return false;
    }
    if (QFileInfo(destination).suffix().isEmpty())
    {
        destination += QStringLiteral(".db");
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QSqlQuery checkpoint(m_database);
    checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL)"));
    if (QFile::exists(destination))
    {
        QFile::remove(destination);
    }
    const bool ok = QFile::copy(m_databasePath, destination);
    if (ok)
    {
        QSettings().setValue(QStringLiteral("maintenance/lastBackup"),
                             QDateTime::currentDateTime().toString(Qt::ISODate));
    }
    recordAudit(QStringLiteral("maintenance.backup"), destination, QString(), ok);
    refreshMaintenance();
    emit notification(ok ? tr("数据库备份完成") : tr("数据库备份失败"), ok);
    return ok;
}

bool IndustrialManager::exportReport(const QString &path, int hours)
{
    QString destination = normalizedFilePath(path);
    if (destination.isEmpty())
    {
        return false;
    }
    if (QFileInfo(destination).suffix().isEmpty())
    {
        destination += QStringLiteral(".csv");
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        emit notification(tr("报表文件无法写入"), false);
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QString(QChar(0xFEFF));
    stream << "ART-Pi Gateway Studio,Industrial Report\n";
    stream << "Generated," << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";
    stream << "Timestamp,Tag,Device,Class,Index,Value,Unit,Quality\n";
    const qint64 from = QDateTime::currentMSecsSinceEpoch() -
                        static_cast<qint64>(qBound(1, hours, 24 * 30)) * 3600000LL;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT samples.timestamp_ms,tags.name,tags.device_index,"
                                 "tags.register_class,tags.value_index,samples.value,tags.unit,samples.quality "
                                 "FROM samples JOIN tags ON tags.id=samples.tag_id "
                                 "WHERE samples.timestamp_ms>=? ORDER BY samples.timestamp_ms"));
    query.addBindValue(from);
    query.exec();
    while (query.next())
    {
        stream << isoTime(query.value(0).toLongLong()) << QLatin1Char(',')
               << QLatin1Char('"')
               << query.value(1).toString().replace(QLatin1Char('"'), QStringLiteral("\"\""))
               << QStringLiteral("\",")
               << query.value(2).toInt() + 1 << QLatin1Char(',')
               << query.value(3).toString() << QLatin1Char(',')
               << query.value(4).toInt() << QLatin1Char(',')
               << query.value(5).toString() << QLatin1Char(',')
               << query.value(6).toString() << QLatin1Char(',')
               << query.value(7).toString() << QLatin1Char('\n');
    }
    file.close();
    const bool ok = file.error() == QFile::NoError;
    recordAudit(QStringLiteral("report.export"), destination,
                tr("%1 小时").arg(hours), ok);
    emit notification(ok ? tr("CSV 报表已导出") : tr("CSV 报表导出失败"), ok);
    return ok;
}

void IndustrialManager::refreshMaintenance()
{
    QVariantMap summary;
    summary.insert(QStringLiteral("database_path"), m_databasePath);
    summary.insert(QStringLiteral("database_bytes"), QFileInfo(m_databasePath).size());
    summary.insert(QStringLiteral("last_backup"),
                   QSettings().value(QStringLiteral("maintenance/lastBackup"), QStringLiteral("从未")).toString());
    for (const auto &entry : {qMakePair(QStringLiteral("tag_count"), QStringLiteral("tags")),
                              qMakePair(QStringLiteral("sample_count"), QStringLiteral("samples")),
                              qMakePair(QStringLiteral("alarm_count"), QStringLiteral("alarms")),
                              qMakePair(QStringLiteral("audit_count"), QStringLiteral("audit")),
                              qMakePair(QStringLiteral("log_count"), QStringLiteral("app_logs"))})
    {
        QSqlQuery query(m_database);
        query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(entry.second));
        summary.insert(entry.first, query.next() ? query.value(0) : 0);
    }
    m_maintenance = summary;
    emit maintenanceChanged();
}

void IndustrialManager::pruneDatabase(qint64 nowMs)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM samples WHERE timestamp_ms<?"));
    query.addBindValue(nowMs - historyRetentionMs);
    query.exec();
    query.exec(QStringLiteral("DELETE FROM app_logs WHERE id NOT IN "
                              "(SELECT id FROM app_logs ORDER BY timestamp_ms DESC LIMIT 5000)"));
    query.exec(QStringLiteral("DELETE FROM audit WHERE id NOT IN "
                              "(SELECT id FROM audit ORDER BY timestamp_ms DESC LIMIT 5000)"));
}

bool IndustrialManager::configureOutlet(const QVariantMap &configuration)
{
    if (!canConfigure())
    {
        emit notification(tr("当前用户无权配置协议出口"), false);
        return false;
    }
    const bool enabled = configuration.value(QStringLiteral("enabled")).toBool();
    const QString host = configuration.value(QStringLiteral("host")).toString().trimmed();
    const int port = configuration.value(QStringLiteral("port"), 1883).toInt();
    const QString topic = configuration.value(QStringLiteral("topic")).toString().trimmed();
    if (enabled && (host.isEmpty() || port < 1 || port > 65535 || topic.isEmpty()))
    {
        emit notification(tr("MQTT 主机、端口或主题无效"), false);
        return false;
    }
    m_outlet = configuration;
    m_outlet.insert(QStringLiteral("type"), QStringLiteral("mqtt"));
    m_outlet.insert(QStringLiteral("port"), port);
    m_outlet.insert(QStringLiteral("intervalSeconds"),
                    qBound(5, configuration.value(QStringLiteral("intervalSeconds"), 10).toInt(), 3600));

    QSettings settings;
    for (const QString &key : {QStringLiteral("enabled"), QStringLiteral("host"),
                               QStringLiteral("port"), QStringLiteral("topic"),
                               QStringLiteral("clientId"), QStringLiteral("username"),
                               QStringLiteral("intervalSeconds")})
    {
        settings.setValue(QStringLiteral("outlet/") + key, m_outlet.value(key));
    }
    m_outletTimer.setInterval(m_outlet.value(QStringLiteral("intervalSeconds")).toInt() * 1000);
    m_outletSocket.abort();
    m_mqttReady = false;
    if (enabled)
    {
        m_outletTimer.start();
        ensureOutletConnected();
    }
    else
    {
        m_outletTimer.stop();
        setOutletState(QStringLiteral("disabled"));
    }
    emit outletChanged();
    recordAudit(QStringLiteral("outlet.configure"), host,
                tr("MQTT %1:%2/%3").arg(host).arg(port).arg(topic), true);
    return true;
}

void IndustrialManager::setOutletState(const QString &state)
{
    if (m_outletState == state)
    {
        return;
    }
    m_outletState = state;
    emit outletStateChanged();
}

void IndustrialManager::ensureOutletConnected()
{
    if (!m_outlet.value(QStringLiteral("enabled")).toBool() ||
        m_outletSocket.state() == QAbstractSocket::ConnectedState ||
        m_outletSocket.state() == QAbstractSocket::ConnectingState)
    {
        return;
    }
    setOutletState(QStringLiteral("connecting"));
    m_outletSocket.connectToHost(m_outlet.value(QStringLiteral("host")).toString(),
                                 static_cast<quint16>(m_outlet.value(QStringLiteral("port"), 1883).toUInt()));
}

QByteArray IndustrialManager::mqttString(const QByteArray &value) const
{
    QByteArray result;
    result.append(static_cast<char>((value.size() >> 8) & 0xFF));
    result.append(static_cast<char>(value.size() & 0xFF));
    result.append(value);
    return result;
}

QByteArray IndustrialManager::mqttRemainingLength(int length) const
{
    QByteArray result;
    do
    {
        int encoded = length % 128;
        length /= 128;
        if (length > 0)
        {
            encoded |= 0x80;
        }
        result.append(static_cast<char>(encoded));
    } while (length > 0);
    return result;
}

void IndustrialManager::sendMqttConnect()
{
    QByteArray variable = mqttString(QByteArrayLiteral("MQTT"));
    variable.append(char(4));
    quint8 flags = 0x02U;
    const QByteArray username = m_outlet.value(QStringLiteral("username")).toString().toUtf8();
    const QByteArray password = m_outlet.value(QStringLiteral("password")).toString().toUtf8();
    if (!username.isEmpty())
    {
        flags |= 0x80U;
    }
    if (!password.isEmpty())
    {
        flags |= 0x40U;
    }
    variable.append(static_cast<char>(flags));
    variable.append(char(0));
    variable.append(char(60));

    QByteArray payload = mqttString(m_outlet.value(QStringLiteral("clientId"),
                                                    QStringLiteral("artpi-gateway-studio"))
                                        .toString().toUtf8());
    if (!username.isEmpty())
    {
        payload += mqttString(username);
    }
    if (!password.isEmpty())
    {
        payload += mqttString(password);
    }
    QByteArray packet(1, char(0x10));
    packet += mqttRemainingLength(variable.size() + payload.size());
    packet += variable;
    packet += payload;
    m_outletSocket.write(packet);
}

void IndustrialManager::sendMqttPublish(const QByteArray &payload)
{
    const QByteArray topic = m_outlet.value(QStringLiteral("topic")).toString().toUtf8();
    QByteArray body = mqttString(topic) + payload;
    QByteArray packet(1, char(0x30));
    packet += mqttRemainingLength(body.size());
    packet += body;
    m_outletSocket.write(packet);
}

QByteArray IndustrialManager::snapshotPayload() const
{
    QVariantMap payload;
    payload.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    payload.insert(QStringLiteral("board"), m_latestSnapshot.value(QStringLiteral("board")));
    payload.insert(QStringLiteral("ip"), m_latestSnapshot.value(QStringLiteral("ip")));
    payload.insert(QStringLiteral("firmware_version"), m_latestSnapshot.value(QStringLiteral("firmware_version")));
    payload.insert(QStringLiteral("rs485"), m_latestSnapshot.value(QStringLiteral("rs485")));
    payload.insert(QStringLiteral("tags"), m_tags);
    return QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact);
}

void IndustrialManager::testOutlet()
{
    if (!m_outlet.value(QStringLiteral("enabled")).toBool())
    {
        emit notification(tr("请先启用 MQTT 出口"), false);
        return;
    }
    publishCurrentSnapshot();
}

void IndustrialManager::publishCurrentSnapshot()
{
    if (m_latestSnapshot.isEmpty() || !m_outlet.value(QStringLiteral("enabled")).toBool())
    {
        return;
    }
    if (!m_mqttReady)
    {
        ensureOutletConnected();
        return;
    }
    sendMqttPublish(snapshotPayload());
    setOutletState(QStringLiteral("online"));
}
