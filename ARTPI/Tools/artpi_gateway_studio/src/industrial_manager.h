#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class IndustrialManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QVariantList alarms READ alarms NOTIFY alarmsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QVariantList audit READ audit NOTIFY auditChanged)
    Q_PROPERTY(QVariantList configVersions READ configVersions NOTIFY configVersionsChanged)
    Q_PROPERTY(QVariantList gateways READ gateways NOTIFY gatewaysChanged)
    Q_PROPERTY(QVariantList users READ users NOTIFY usersChanged)
    Q_PROPERTY(QVariantList persistentLogs READ persistentLogs NOTIFY persistentLogsChanged)
    Q_PROPERTY(QVariantMap maintenance READ maintenance NOTIFY maintenanceChanged)
    Q_PROPERTY(QVariantMap outlet READ outlet NOTIFY outletChanged)
    Q_PROPERTY(QString outletState READ outletState NOTIFY outletStateChanged)
    Q_PROPERTY(int activeAlarmCount READ activeAlarmCount NOTIFY alarmsChanged)
    Q_PROPERTY(QString currentUser READ currentUser NOTIFY securityChanged)
    Q_PROPERTY(QString currentRole READ currentRole NOTIFY securityChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY securityChanged)
    Q_PROPERTY(bool securityInitialized READ securityInitialized NOTIFY securityChanged)
    Q_PROPERTY(bool canOperate READ canOperate NOTIFY securityChanged)
    Q_PROPERTY(bool canConfigure READ canConfigure NOTIFY securityChanged)

public:
    explicit IndustrialManager(QObject *parent = nullptr);
    ~IndustrialManager() override;

    QString databasePath() const;
    QVariantList tags() const;
    QVariantList alarms() const;
    QVariantList history() const;
    QVariantList audit() const;
    QVariantList configVersions() const;
    QVariantList gateways() const;
    QVariantList users() const;
    QVariantList persistentLogs() const;
    QVariantMap maintenance() const;
    QVariantMap outlet() const;
    QString outletState() const;
    int activeAlarmCount() const;
    QString currentUser() const;
    QString currentRole() const;
    bool authenticated() const;
    bool securityInitialized() const;
    bool canOperate() const;
    bool canConfigure() const;

    Q_INVOKABLE void ingestSnapshot(const QVariantMap &snapshot);
    Q_INVOKABLE int addTag(const QVariantMap &definition);
    Q_INVOKABLE bool updateTag(int id, const QVariantMap &definition);
    Q_INVOKABLE bool removeTag(int id);
    Q_INVOKABLE void selectHistory(int tagId, int hours = 24);
    Q_INVOKABLE bool acknowledgeAlarm(int alarmId);
    Q_INVOKABLE int acknowledgeAllAlarms();

    Q_INVOKABLE void recordAudit(const QString &action,
                                 const QString &target,
                                 const QString &detail,
                                 bool success = true);
    Q_INVOKABLE void recordLog(const QString &level,
                               const QString &source,
                               const QString &message);
    Q_INVOKABLE void clearPersistentLogs();

    Q_INVOKABLE int saveConfigurationVersion(const QString &label,
                                              const QString &endpoint,
                                              const QVariantMap &configuration);
    Q_INVOKABLE QVariantMap restoreConfigurationVersion(int id) const;
    Q_INVOKABLE bool removeConfigurationVersion(int id);

    Q_INVOKABLE int addGateway(const QString &name,
                               const QString &endpoint,
                               const QString &notes = QString());
    Q_INVOKABLE bool removeGateway(int id);
    Q_INVOKABLE void activateGateway(int id);

    Q_INVOKABLE bool initializeSecurity(const QString &password);
    Q_INVOKABLE bool login(const QString &username, const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE bool saveUser(const QString &username,
                              const QString &password,
                              const QString &role,
                              bool enabled = true);
    Q_INVOKABLE bool removeUser(const QString &username);

    Q_INVOKABLE bool backupDatabase(const QString &path);
    Q_INVOKABLE bool exportReport(const QString &path, int hours = 24);
    Q_INVOKABLE void refreshMaintenance();

    Q_INVOKABLE bool configureOutlet(const QVariantMap &configuration);
    Q_INVOKABLE void testOutlet();
    Q_INVOKABLE void publishCurrentSnapshot();

signals:
    void tagsChanged();
    void alarmsChanged();
    void historyChanged();
    void auditChanged();
    void configVersionsChanged();
    void gatewaysChanged();
    void usersChanged();
    void persistentLogsChanged();
    void maintenanceChanged();
    void outletChanged();
    void outletStateChanged();
    void securityChanged();
    void notification(const QString &message, bool ok);
    void gatewayActivationRequested(const QString &endpoint);
    void configurationRestoreRequested(const QVariantMap &configuration);

private:
    bool openDatabase();
    bool createSchema();
    void loadSecurityState();
    void loadOutletSettings();
    void refreshTags();
    void refreshAlarms();
    void refreshAudit();
    void refreshConfigVersions();
    void refreshGateways();
    void refreshUsers();
    void refreshPersistentLogs();
    void seedDefaultTags(const QVariantMap &snapshot);
    void storeSamples(const QVariantMap &snapshot, qint64 timestampMs);
    void evaluateAlarm(int tagId,
                       const QString &tagName,
                       double value,
                       bool lowEnabled,
                       double lowLimit,
                       bool highEnabled,
                       double highLimit,
                       qint64 timestampMs);
    void pruneDatabase(qint64 nowMs);
    QVariantMap normalizedTag(const QVariantMap &definition) const;
    bool validateTag(const QVariantMap &definition, QString *error) const;
    QString normalizedFilePath(const QString &path) const;
    QString derivePassword(const QString &password, const QByteArray &salt) const;
    void setOutletState(const QString &state);
    void ensureOutletConnected();
    void sendMqttConnect();
    void sendMqttPublish(const QByteArray &payload);
    QByteArray mqttString(const QByteArray &value) const;
    QByteArray mqttRemainingLength(int length) const;
    QByteArray snapshotPayload() const;

    QString m_connectionName;
    QString m_databasePath;
    QSqlDatabase m_database;
    QVariantList m_tags;
    QVariantList m_alarms;
    QVariantList m_history;
    QVariantList m_audit;
    QVariantList m_configVersions;
    QVariantList m_gateways;
    QVariantList m_users;
    QVariantList m_persistentLogs;
    QVariantMap m_maintenance;
    QVariantMap m_outlet;
    QVariantMap m_latestSnapshot;
    QVariantMap m_latestValues;
    QVariantMap m_latestQuality;
    QString m_outletState = QStringLiteral("disabled");
    QString m_currentUser = QStringLiteral("local");
    QString m_currentRole = QStringLiteral("administrator");
    bool m_authenticated = true;
    bool m_securityInitialized = false;
    bool m_mqttReady = false;
    qint64 m_lastSampleMs = 0;
    qint64 m_lastPruneMs = 0;
    qint64 m_lastGatewayUpdateMs = 0;
    QTcpSocket m_outletSocket;
    QTimer m_outletTimer;
};
