#pragma once

#include "device_list_model.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QNetworkReply;

class ArtPiClient final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY endpointChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool autoRefresh READ autoRefresh WRITE setAutoRefresh NOTIFY autoRefreshChanged)
    Q_PROPERTY(int refreshInterval READ refreshInterval WRITE setRefreshInterval NOTIFY refreshIntervalChanged)
    Q_PROPERTY(QString connectionText READ connectionText NOTIFY connectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString boardName READ boardName NOTIFY telemetryChanged)
    Q_PROPERTY(QString boardIp READ boardIp NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong uptimeSeconds READ uptimeSeconds NOTIFY telemetryChanged)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap ethernet READ ethernet NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap rs485 READ rs485 NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap lastCommand READ lastCommand NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    Q_PROPERTY(QVariantList logs READ logs NOTIFY logsChanged)
    Q_PROPERTY(DeviceListModel *devices READ devices CONSTANT)
    Q_PROPERTY(int onlineDeviceCount READ onlineDeviceCount NOTIFY telemetryChanged)
    Q_PROPERTY(int offlineDeviceCount READ offlineDeviceCount NOTIFY telemetryChanged)
    Q_PROPERTY(QString lastUpdated READ lastUpdated NOTIFY telemetryChanged)

public:
    explicit ArtPiClient(QObject *parent = nullptr);

    QString endpoint() const;
    void setEndpoint(const QString &endpoint);
    bool connected() const;
    bool busy() const;
    bool autoRefresh() const;
    void setAutoRefresh(bool enabled);
    int refreshInterval() const;
    void setRefreshInterval(int milliseconds);
    QString connectionText() const;
    QString lastError() const;
    QString boardName() const;
    QString boardIp() const;
    qulonglong uptimeSeconds() const;
    QString uptimeText() const;
    QVariantMap ethernet() const;
    QVariantMap rs485() const;
    QVariantMap lastCommand() const;
    QVariantMap config() const;
    QVariantList logs() const;
    DeviceListModel *devices();
    int onlineDeviceCount() const;
    int offlineDeviceCount() const;
    QString lastUpdated() const;

    Q_INVOKABLE void connectNow();
    Q_INVOKABLE void disconnectFromBoard();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadConfig();
    Q_INVOKABLE void saveConfiguration(const QVariantMap &general,
                                       const QVariantList &devices);
    Q_INVOKABLE void sendCommand(int deviceIndex, int type, int address, int value);
    Q_INVOKABLE void clearLogs();

signals:
    void endpointChanged();
    void connectionChanged();
    void busyChanged();
    void autoRefreshChanged();
    void refreshIntervalChanged();
    void lastErrorChanged();
    void telemetryChanged();
    void configChanged();
    void logsChanged();
    void statusUpdated();
    void configUpdated();
    void configurationSaved();
    void commandQueued(qulonglong commandId);
    void commandCompleted(bool ok, const QString &message);

private:
    enum class RequestKind
    {
        Status,
        ConfigRead,
        ConfigWrite,
        Command
    };

    QUrl apiUrl(const QString &path) const;
    QNetworkRequest makeRequest(const QString &path) const;
    void beginRequest();
    void finishRequest();
    void enqueueRequest(std::function<void()> request);
    void startNextRequest();
    void handleReply(QNetworkReply *reply, RequestKind kind);
    void applyStatus(const QVariantMap &root);
    void applyConfig(const QVariantMap &root);
    void setConnected(bool connected);
    void setLastError(const QString &error);
    void addLog(const QString &level, const QString &message);
    static int mapInt(const QVariantMap &map, const QString &key, int fallback = 0);
    static QVariant deviceField(const QVariantMap &device,
                                const QString &camelCase,
                                const QString &snakeCase,
                                const QVariant &fallback = {});

    QNetworkAccessManager m_network;
    QTimer m_refreshTimer;
    DeviceListModel m_devices;
    QString m_endpoint;
    bool m_connected = false;
    bool m_autoRefresh = true;
    bool m_statusInFlight = false;
    bool m_transportBusy = false;
    int m_pendingRequests = 0;
    int m_refreshInterval = 1000;
    QString m_lastError;
    QString m_boardName;
    QString m_boardIp;
    qulonglong m_uptimeSeconds = 0;
    QVariantMap m_ethernet;
    QVariantMap m_rs485;
    QVariantMap m_lastCommand;
    QVariantMap m_config;
    QVariantList m_logs;
    int m_onlineDeviceCount = 0;
    int m_offlineDeviceCount = 0;
    QString m_lastUpdated;
    QQueue<std::function<void()>> m_requestQueue;
};
