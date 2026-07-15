#include "artpi_client.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSet>
#include <QUrl>

#include <utility>

namespace
{
constexpr int kRequestTimeoutMs = 4000;
constexpr int kMaxLogEntries = 200;
}

ArtPiClient::ArtPiClient(QObject *parent)
    : QObject(parent)
    , m_devices(this)
{
    QSettings settings;
    m_endpoint = settings.value(QStringLiteral("connection/endpoint"),
                                QStringLiteral("http://192.168.1.20")).toString();
    m_refreshInterval = settings.value(QStringLiteral("connection/refreshInterval"), 1000)
                            .toInt();
    m_refreshInterval = qBound(500, m_refreshInterval, 10000);

    m_refreshTimer.setInterval(m_refreshInterval);
    m_refreshTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ArtPiClient::refresh);
}

QString ArtPiClient::endpoint() const
{
    return m_endpoint;
}

void ArtPiClient::setEndpoint(const QString &endpoint)
{
    QString normalized = endpoint.trimmed();
    if (normalized.isEmpty())
    {
        return;
    }
    if (!normalized.contains(QStringLiteral("://")))
    {
        normalized.prepend(QStringLiteral("http://"));
    }
    while (normalized.endsWith(QLatin1Char('/')))
    {
        normalized.chop(1);
    }
    if (normalized == m_endpoint)
    {
        return;
    }

    m_endpoint = normalized;
    QSettings().setValue(QStringLiteral("connection/endpoint"), m_endpoint);
    emit endpointChanged();
}

bool ArtPiClient::connected() const
{
    return m_connected;
}

bool ArtPiClient::busy() const
{
    return m_pendingRequests > 0;
}

bool ArtPiClient::autoRefresh() const
{
    return m_autoRefresh;
}

void ArtPiClient::setAutoRefresh(bool enabled)
{
    if (m_autoRefresh == enabled)
    {
        return;
    }
    m_autoRefresh = enabled;
    if (!enabled)
    {
        m_refreshTimer.stop();
    }
    else if (m_connected)
    {
        m_refreshTimer.start();
    }
    emit autoRefreshChanged();
}

int ArtPiClient::refreshInterval() const
{
    return m_refreshInterval;
}

void ArtPiClient::setRefreshInterval(int milliseconds)
{
    const int bounded = qBound(500, milliseconds, 10000);
    if (bounded == m_refreshInterval)
    {
        return;
    }
    m_refreshInterval = bounded;
    m_refreshTimer.setInterval(bounded);
    QSettings().setValue(QStringLiteral("connection/refreshInterval"), bounded);
    emit refreshIntervalChanged();
}

QString ArtPiClient::connectionText() const
{
    if (m_connected)
    {
        return tr("已连接");
    }
    return m_lastError.isEmpty() ? tr("未连接") : tr("连接异常");
}

QString ArtPiClient::connectionState() const
{
    if (m_connected)
    {
        return QStringLiteral("online");
    }
    if (m_consecutiveFailures > 0)
    {
        return QStringLiteral("reconnecting");
    }
    return QStringLiteral("offline");
}

int ArtPiClient::consecutiveFailures() const
{
    return m_consecutiveFailures;
}

QString ArtPiClient::lastError() const
{
    return m_lastError;
}

int ArtPiClient::apiVersion() const
{
    return m_apiVersion;
}

QString ArtPiClient::firmwareVersion() const
{
    return m_firmwareVersion;
}

QString ArtPiClient::boardName() const
{
    return m_boardName;
}

QString ArtPiClient::boardIp() const
{
    return m_boardIp;
}

qulonglong ArtPiClient::uptimeSeconds() const
{
    return m_uptimeSeconds;
}

QString ArtPiClient::uptimeText() const
{
    const qulonglong days = m_uptimeSeconds / 86400ULL;
    const qulonglong hours = (m_uptimeSeconds % 86400ULL) / 3600ULL;
    const qulonglong minutes = (m_uptimeSeconds % 3600ULL) / 60ULL;
    const qulonglong seconds = m_uptimeSeconds % 60ULL;
    if (days > 0)
    {
        return tr("%1天 %2:%3:%4")
            .arg(days)
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QVariantMap ArtPiClient::ethernet() const
{
    return m_ethernet;
}

QVariantMap ArtPiClient::rs485() const
{
    return m_rs485;
}

QVariantMap ArtPiClient::lastCommand() const
{
    return m_lastCommand;
}

QVariantMap ArtPiClient::statusSnapshot() const
{
    return m_statusSnapshot;
}

QVariantMap ArtPiClient::config() const
{
    return m_config;
}

QVariantList ArtPiClient::logs() const
{
    return m_logs;
}

DeviceListModel *ArtPiClient::devices()
{
    return &m_devices;
}

int ArtPiClient::onlineDeviceCount() const
{
    return m_onlineDeviceCount;
}

int ArtPiClient::offlineDeviceCount() const
{
    return m_offlineDeviceCount;
}

QString ArtPiClient::lastUpdated() const
{
    return m_lastUpdated;
}

void ArtPiClient::connectNow()
{
    m_refreshTimer.stop();
    setLastError({});
    addLog(QStringLiteral("info"), tr("正在连接 %1").arg(m_endpoint));
    refresh();
    loadConfig();
}

void ArtPiClient::disconnectFromBoard()
{
    m_refreshTimer.stop();
    setConnected(false);
    m_statusInFlight = false;
    m_consecutiveFailures = 0;
    m_devices.clear();
    m_onlineDeviceCount = 0;
    m_offlineDeviceCount = 0;
    addLog(QStringLiteral("info"), tr("已断开自动刷新"));
    emit telemetryChanged();
}

void ArtPiClient::refresh()
{
    if (m_statusInFlight)
    {
        return;
    }

    m_statusInFlight = true;
    beginRequest();
    enqueueRequest([this]() {
        QNetworkReply *reply = m_network.get(makeRequest(QStringLiteral("/api/status")));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_statusInFlight = false;
            handleReply(reply, RequestKind::Status);
            m_transportBusy = false;
            QTimer::singleShot(20, this, &ArtPiClient::startNextRequest);
        });
    });
}

void ArtPiClient::loadConfig()
{
    beginRequest();
    enqueueRequest([this]() {
        QNetworkReply *reply = m_network.get(makeRequest(QStringLiteral("/api/config")));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            handleReply(reply, RequestKind::ConfigRead);
            m_transportBusy = false;
            QTimer::singleShot(20, this, &ArtPiClient::startNextRequest);
        });
    });
}

void ArtPiClient::saveConfiguration(const QVariantMap &general,
                                    const QVariantList &devices)
{
    const QString validationError = validateConfiguration(general, devices);
    if (!validationError.isEmpty())
    {
        const QString message = tr("配置未发送：%1").arg(validationError);
        setLastError(message);
        addLog(QStringLiteral("error"), message);
        emit configurationFailed(message);
        emit commandCompleted(false, message);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("rs485_role"), mapInt(general, QStringLiteral("rs485Role"), 1));
    body.insert(QStringLiteral("modbus_unit_id"), mapInt(general, QStringLiteral("modbusUnitId"), 2));
    body.insert(QStringLiteral("master_device_count"),
                qBound(1, mapInt(general, QStringLiteral("masterDeviceCount"), devices.size()), 10));
    body.insert(QStringLiteral("offline_probe_s"),
                mapInt(general, QStringLiteral("offlineProbeSeconds"), 60));
    body.insert(QStringLiteral("poll_period_ms"),
                mapInt(general, QStringLiteral("pollPeriodMs"), 1000));
    body.insert(QStringLiteral("red_led_on"), mapInt(general, QStringLiteral("redLedOn"), 0));
    body.insert(QStringLiteral("buzzer_on"), mapInt(general, QStringLiteral("buzzerOn"), 0));

    const QStringList prefixes = {
        QStringLiteral("coil"), QStringLiteral("discrete"),
        QStringLiteral("holding"), QStringLiteral("input")
    };
    const int activeDeviceCount = body.value(QStringLiteral("master_device_count")).toInt();
    for (int index = 0; index < activeDeviceCount; ++index)
    {
        const QVariantMap device = devices.at(index).toMap();
        const QString root = QStringLiteral("d%1_").arg(index);
        body.insert(root + QStringLiteral("unit_id"),
                    deviceField(device, QStringLiteral("unitId"), QStringLiteral("unit_id"), index + 1).toInt());
        body.insert(root + QStringLiteral("timeout_ms"),
                    deviceField(device, QStringLiteral("timeoutMs"), QStringLiteral("timeout_ms"), 200).toInt());
        for (const QString &prefix : prefixes)
        {
            body.insert(root + prefix + QStringLiteral("_address"),
                        deviceField(device,
                                    prefix + QStringLiteral("Address"),
                                    prefix + QStringLiteral("_address"),
                                    0).toInt());
            body.insert(root + prefix + QStringLiteral("_quantity"),
                        deviceField(device,
                                    prefix + QStringLiteral("Quantity"),
                                    prefix + QStringLiteral("_quantity"),
                                    0).toInt());
        }
    }

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    beginRequest();
    enqueueRequest([this, payload]() {
        QNetworkReply *reply = m_network.post(makeRequest(QStringLiteral("/api/config")), payload);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            handleReply(reply, RequestKind::ConfigWrite);
            m_transportBusy = false;
            QTimer::singleShot(20, this, &ArtPiClient::startNextRequest);
        });
    });
}

void ArtPiClient::sendCommand(int deviceIndex, int type, int address, int value)
{
    const int normalizedDevice = qBound(0, deviceIndex, 9);
    const int normalizedType = qBound(0, type, 1);
    const int normalizedAddress = qBound(0, address, 65535);
    const int normalizedValue = qBound(0, value, 65535);
    QJsonObject body;
    body.insert(QStringLiteral("device_index"), normalizedDevice);
    body.insert(QStringLiteral("type"), normalizedType);
    body.insert(QStringLiteral("address"), normalizedAddress);
    body.insert(QStringLiteral("value"), normalizedValue);

    emit commandSubmitted(normalizedDevice,
                          normalizedType,
                          normalizedAddress,
                          normalizedValue);

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    beginRequest();
    enqueueRequest([this, payload]() {
        QNetworkReply *reply = m_network.post(makeRequest(QStringLiteral("/api/rs485/command")), payload);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            handleReply(reply, RequestKind::Command);
            m_transportBusy = false;
            QTimer::singleShot(20, this, &ArtPiClient::startNextRequest);
        });
    });
}

void ArtPiClient::clearLogs()
{
    if (m_logs.isEmpty())
    {
        return;
    }
    m_logs.clear();
    emit logsChanged();
}

QUrl ArtPiClient::apiUrl(const QString &path) const
{
    QUrl url(m_endpoint);
    url.setPath(path);
    url.setQuery({});
    url.setFragment({});
    return url;
}

QNetworkRequest ArtPiClient::makeRequest(const QString &path) const
{
    QNetworkRequest request(apiUrl(path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "ART-Pi-Gateway-Studio/0.1");
    request.setTransferTimeout(kRequestTimeoutMs);
    return request;
}

void ArtPiClient::beginRequest()
{
    const bool wasBusy = busy();
    ++m_pendingRequests;
    if (!wasBusy)
    {
        emit busyChanged();
    }
}

void ArtPiClient::finishRequest()
{
    const bool wasBusy = busy();
    m_pendingRequests = qMax(0, m_pendingRequests - 1);
    if (wasBusy != busy())
    {
        emit busyChanged();
    }
}

void ArtPiClient::enqueueRequest(std::function<void()> request)
{
    m_requestQueue.enqueue(std::move(request));
    startNextRequest();
}

void ArtPiClient::startNextRequest()
{
    if (m_transportBusy || m_requestQueue.isEmpty())
    {
        return;
    }

    m_transportBusy = true;
    std::function<void()> request = m_requestQueue.dequeue();
    request();
}

void ArtPiClient::handleReply(QNetworkReply *reply, RequestKind kind)
{
    finishRequest();
    const QByteArray payload = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool transportOk = reply->error() == QNetworkReply::NoError;
    const QString transportError = reply->errorString();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QVariantMap root = document.isObject() ? document.object().toVariantMap() : QVariantMap{};
    const bool jsonOk = parseError.error == QJsonParseError::NoError && document.isObject();
    const bool apiOk = !root.contains(QStringLiteral("ok")) || root.value(QStringLiteral("ok")).toBool();

    if (!transportOk || status < 200 || status >= 300 || !jsonOk || !apiOk)
    {
        QString message;
        if (jsonOk && root.contains(QStringLiteral("error")))
        {
            message = root.value(QStringLiteral("error")).toString();
            const QString field = root.value(QStringLiteral("field")).toString();
            const QString reason = root.value(QStringLiteral("reason")).toString();
            if (!field.isEmpty())
            {
                message += tr("（字段：%1").arg(field);
                if (!reason.isEmpty())
                {
                    message += tr("，原因：%1").arg(reason);
                }
                message += QLatin1Char('）');
            }
            if (status > 0)
            {
                message += tr(" [HTTP %1]").arg(status);
            }
        }
        else if (!transportOk)
        {
            message = transportError;
        }
        else if (!jsonOk)
        {
            message = tr("JSON 解析失败：%1").arg(parseError.errorString());
        }
        else
        {
            message = root.value(QStringLiteral("error"),
                                 tr("HTTP %1 请求失败").arg(status)).toString();
        }

        setLastError(message);
        if (kind == RequestKind::Status)
        {
            ++m_consecutiveFailures;
            setConnected(false);
            if (m_autoRefresh && !m_refreshTimer.isActive())
            {
                m_refreshTimer.start();
            }
        }
        addLog(QStringLiteral("error"), message);
        const QString operation = kind == RequestKind::Status ? QStringLiteral("status")
                                  : kind == RequestKind::ConfigRead ? QStringLiteral("config-read")
                                  : kind == RequestKind::ConfigWrite ? QStringLiteral("config-write")
                                                                    : QStringLiteral("command");
        emit requestFailed(operation, status, message);
        if ((kind == RequestKind::Command) || (kind == RequestKind::ConfigWrite))
        {
            emit commandCompleted(false, message);
        }
        if (kind == RequestKind::ConfigWrite)
        {
            emit configurationFailed(message);
        }
        return;
    }

    setLastError({});

    switch (kind)
    {
    case RequestKind::Status:
        applyStatus(root);
        break;
    case RequestKind::ConfigRead:
        applyConfig(root);
        break;
    case RequestKind::ConfigWrite:
        addLog(QStringLiteral("success"), tr("板端配置已保存"));
        emit configurationSaved();
        emit commandCompleted(true, tr("配置已保存"));
        loadConfig();
        refresh();
        break;
    case RequestKind::Command:
    {
        const qulonglong commandId = root.value(QStringLiteral("command_id")).toULongLong();
        const QString message = tr("命令 #%1 已进入优先队列")
                                    .arg(commandId);
        addLog(QStringLiteral("success"), message);
        emit commandQueued(commandId);
        emit commandCompleted(true, message);
        QTimer::singleShot(120, this, &ArtPiClient::refresh);
        break;
    }
    }
}

void ArtPiClient::applyStatus(const QVariantMap &root)
{
    const bool firstConnection = !m_connected;
    m_statusSnapshot = root;
    m_apiVersion = root.value(QStringLiteral("api_version")).toInt();
    m_firmwareVersion = root.value(QStringLiteral("firmware_version")).toString();
    m_boardName = root.value(QStringLiteral("board")).toString();
    m_boardIp = root.value(QStringLiteral("ip")).toString();
    m_uptimeSeconds = root.value(QStringLiteral("uptime_s")).toULongLong();
    m_ethernet = root.value(QStringLiteral("ethernet")).toMap();
    m_rs485 = root.value(QStringLiteral("rs485")).toMap();
    m_lastCommand = m_rs485.value(QStringLiteral("last_command")).toMap();

    const QVariantList deviceList = m_rs485.value(QStringLiteral("devices")).toList();
    m_devices.updateFromVariantList(deviceList);
    m_onlineDeviceCount = 0;
    m_offlineDeviceCount = 0;
    for (const QVariant &entry : deviceList)
    {
        const QString state = entry.toMap().value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("online"))
        {
            ++m_onlineDeviceCount;
        }
        else if (state == QStringLiteral("offline"))
        {
            ++m_offlineDeviceCount;
        }
    }
    m_lastUpdated = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_consecutiveFailures = 0;
    setLastError({});
    setConnected(true);
    if (firstConnection)
    {
        addLog(QStringLiteral("success"), tr("已连接 %1（%2）").arg(m_boardName, m_boardIp));
    }
    if (m_autoRefresh && !m_refreshTimer.isActive())
    {
        m_refreshTimer.start();
    }
    emit telemetryChanged();
    emit statusUpdated();
}

void ArtPiClient::applyConfig(const QVariantMap &root)
{
    m_config = root;
    emit configChanged();
    emit configUpdated();
}

void ArtPiClient::setConnected(bool connected)
{
    if (m_connected == connected)
    {
        return;
    }
    m_connected = connected;
    emit connectionChanged();
}

void ArtPiClient::setLastError(const QString &error)
{
    if (m_lastError == error)
    {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
    emit connectionChanged();
}

void ArtPiClient::addLog(const QString &level, const QString &message)
{
    if (!m_logs.isEmpty())
    {
        const QVariantMap previous = m_logs.constLast().toMap();
        if (previous.value(QStringLiteral("level")).toString() == level &&
            previous.value(QStringLiteral("message")).toString() == message)
        {
            return;
        }
    }
    m_logs.append(QVariantMap{
        {QStringLiteral("time"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))},
        {QStringLiteral("level"), level},
        {QStringLiteral("message"), message}
    });
    while (m_logs.size() > kMaxLogEntries)
    {
        m_logs.removeFirst();
    }
    emit logsChanged();
    emit logAdded(level, message);
}

int ArtPiClient::mapInt(const QVariantMap &map, const QString &key, int fallback)
{
    return map.contains(key) ? map.value(key).toInt() : fallback;
}

QVariant ArtPiClient::deviceField(const QVariantMap &device,
                                  const QString &camelCase,
                                  const QString &snakeCase,
                                  const QVariant &fallback)
{
    if (device.contains(camelCase))
    {
        return device.value(camelCase);
    }
    if (device.contains(snakeCase))
    {
        return device.value(snakeCase);
    }
    return fallback;
}

QString ArtPiClient::validateConfiguration(const QVariantMap &general,
                                           const QVariantList &devices) const
{
    const int role = mapInt(general, QStringLiteral("rs485Role"), -1);
    const int unitId = mapInt(general, QStringLiteral("modbusUnitId"), 0);
    const int deviceCount = mapInt(general,
                                   QStringLiteral("masterDeviceCount"),
                                   devices.size());
    const int offlineProbe = mapInt(general, QStringLiteral("offlineProbeSeconds"), 0);
    const int pollPeriod = mapInt(general, QStringLiteral("pollPeriodMs"), 0);
    const int redLed = mapInt(general, QStringLiteral("redLedOn"), 0);
    const int buzzer = mapInt(general, QStringLiteral("buzzerOn"), 0);

    if (role < 0 || role > 1)
    {
        return tr("RS485 角色无效");
    }
    if (unitId < 1 || unitId > 247)
    {
        return tr("本机 Unit ID 必须为 1~247");
    }
    if (deviceCount < 1 || deviceCount > 10 || devices.size() < deviceCount)
    {
        return tr("从机数量与轮询表不一致");
    }
    if (offlineProbe != 60 && offlineProbe != 300)
    {
        return tr("离线探测周期只能为 60 或 300 秒");
    }
    if (pollPeriod < 100 || pollPeriod > 60000)
    {
        return tr("轮询周期必须为 100~60000 ms");
    }
    if (redLed < 0 || redLed > 1 || buzzer < 0 || buzzer > 1)
    {
        return tr("本地输出值无效");
    }

    QSet<int> unitIds;
    const QStringList prefixes = {
        QStringLiteral("coil"), QStringLiteral("discrete"),
        QStringLiteral("holding"), QStringLiteral("input")
    };
    for (int index = 0; index < deviceCount; ++index)
    {
        const QVariantMap device = devices.at(index).toMap();
        const int deviceUnitId = deviceField(device,
                                             QStringLiteral("unitId"),
                                             QStringLiteral("unit_id"),
                                             0).toInt();
        const int timeout = deviceField(device,
                                        QStringLiteral("timeoutMs"),
                                        QStringLiteral("timeout_ms"),
                                        0).toInt();
        if (deviceUnitId < 1 || deviceUnitId > 247)
        {
            return tr("第 %1 台从机 Unit ID 无效").arg(index + 1);
        }
        if (unitIds.contains(deviceUnitId))
        {
            return tr("Unit ID %1 重复").arg(deviceUnitId);
        }
        unitIds.insert(deviceUnitId);
        if (timeout < 20 || timeout > 5000)
        {
            return tr("第 %1 台从机超时必须为 20~5000 ms").arg(index + 1);
        }

        int enabledRanges = 0;
        for (const QString &prefix : prefixes)
        {
            const int address = deviceField(device,
                                            prefix + QStringLiteral("Address"),
                                            prefix + QStringLiteral("_address"),
                                            -1).toInt();
            const int quantity = deviceField(device,
                                             prefix + QStringLiteral("Quantity"),
                                             prefix + QStringLiteral("_quantity"),
                                             -1).toInt();
            if (address < 0 || address > 65535 || quantity < 0 || quantity > 16 ||
                (quantity > 0 && address + quantity > 65536))
            {
                return tr("第 %1 台从机 %2 地址范围无效")
                    .arg(index + 1)
                    .arg(prefix);
            }
            if (quantity > 0)
            {
                ++enabledRanges;
            }
        }
        if (enabledRanges == 0)
        {
            return tr("第 %1 台从机至少启用一类寄存器").arg(index + 1);
        }
    }
    return {};
}
