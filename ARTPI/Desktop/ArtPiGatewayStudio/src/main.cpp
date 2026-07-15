#include "artpi_client.h"
#include "industrial_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTextStream>

namespace
{
bool hasArgument(int argc, char *argv[], const QByteArray &argument)
{
    for (int index = 1; index < argc; ++index)
    {
        if (QByteArray(argv[index]) == argument)
        {
            return true;
        }
    }
    return false;
}

QString valueAfterArgument(int argc,
                           char *argv[],
                           const QByteArray &argument,
                           const QString &fallback)
{
    for (int index = 1; index + 1 < argc; ++index)
    {
        if (QByteArray(argv[index]) == argument)
        {
            return QString::fromLocal8Bit(argv[index + 1]);
        }
    }
    return fallback;
}

int runSelfTest(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Leduo Lab"));
    QCoreApplication::setApplicationName(QStringLiteral("ART-Pi Gateway Studio Self Test"));

    ArtPiClient client;
    client.setAutoRefresh(false);
    client.setEndpoint(valueAfterArgument(argc,
                                          argv,
                                          QByteArrayLiteral("--self-test"),
                                          QStringLiteral("http://192.168.1.20")));

    bool statusReady = false;
    bool configReady = false;
    const auto finishIfReady = [&]() {
        if (!statusReady || !configReady)
        {
            return;
        }
        qInfo().noquote() << QStringLiteral("SELF-TEST PASS board=%1 ip=%2 devices=%3 uptime=%4")
                                 .arg(client.boardName(), client.boardIp())
                                 .arg(client.devices()->rowCount())
                                 .arg(client.uptimeSeconds());
        application.exit(0);
    };

    QObject::connect(&client, &ArtPiClient::statusUpdated, &application, [&]() {
        statusReady = !client.boardName().isEmpty() &&
                      !client.boardIp().isEmpty() &&
                      client.rs485().contains(QStringLiteral("devices"));
        finishIfReady();
    });
    QObject::connect(&client, &ArtPiClient::configUpdated, &application, [&]() {
        configReady = client.config().value(QStringLiteral("ok")).toBool() &&
                      client.config().contains(QStringLiteral("devices"));
        finishIfReady();
    });

    QTimer::singleShot(8000, &application, [&]() {
        qCritical().noquote() << QStringLiteral("SELF-TEST FAIL status=%1 config=%2 error=%3")
                                     .arg(statusReady)
                                     .arg(configReady)
                                     .arg(client.lastError());
        application.exit(2);
    });
    client.connectNow();
    return application.exec();
}

int runPayloadSelfTest(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"qml(
        import QtQml
        import QtQml.Models
        QtObject {
            property ListModel rows: ListModel {
                ListElement {
                    unitId: 3
                    timeoutMs: 200
                    coilAddress: 0
                    coilQuantity: 0
                    discreteAddress: 0
                    discreteQuantity: 0
                    holdingAddress: 0
                    holdingQuantity: 2
                    inputAddress: 0
                    inputQuantity: 2
                }
            }
            function firstSnapshot() {
                var device = rows.get(0)
                return {
                    "unitId": Number(device.unitId),
                    "timeoutMs": Number(device.timeoutMs),
                    "coilAddress": Number(device.coilAddress),
                    "coilQuantity": Number(device.coilQuantity),
                    "discreteAddress": Number(device.discreteAddress),
                    "discreteQuantity": Number(device.discreteQuantity),
                    "holdingAddress": Number(device.holdingAddress),
                    "holdingQuantity": Number(device.holdingQuantity),
                    "inputAddress": Number(device.inputAddress),
                    "inputQuantity": Number(device.inputQuantity)
                }
            }
        }
    )qml", QUrl(QStringLiteral("inmemory:/payload_test.qml")));

    QElapsedTimer loadTimer;
    loadTimer.start();
    while (component.isLoading() && loadTimer.elapsed() < 2000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    QScopedPointer<QObject> fixture(component.create());
    if (!fixture)
    {
        QFile traceFile(QDir::temp().filePath(QStringLiteral("artpi_gateway_payload_self_test.log")));
        if (traceFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            traceFile.write(component.errorString().toUtf8());
        }
        qCritical().noquote() << component.errorString();
        return 2;
    }

    QVariant rowSnapshot;
    if (!QMetaObject::invokeMethod(fixture.data(),
                                   "firstSnapshot",
                                   Q_RETURN_ARG(QVariant, rowSnapshot)))
    {
        qCritical() << "PAYLOAD-SELF-TEST FAIL could not read ListModel row";
        return 3;
    }

    const QVariantMap general{
        {QStringLiteral("rs485Role"), 1},
        {QStringLiteral("modbusUnitId"), 5},
        {QStringLiteral("masterDeviceCount"), 1},
        {QStringLiteral("offlineProbeSeconds"), 60},
        {QStringLiteral("pollPeriodMs"), 750},
        {QStringLiteral("redLedOn"), 0},
        {QStringLiteral("buzzerOn"), 0}
    };
    ArtPiClient client;
    client.setAutoRefresh(false);
    client.setEndpoint(QStringLiteral("http://127.0.0.1:1"));

    bool rejected = false;
    QString rejection;
    QObject::connect(&client, &ArtPiClient::configurationFailed,
                     &application, [&](const QString &message) {
        rejected = true;
        rejection = message;
    });
    client.saveConfiguration(general, QVariantList{rowSnapshot});
    if (rejected)
    {
        qCritical().noquote() << QStringLiteral("PAYLOAD-SELF-TEST FAIL %1").arg(rejection);
        return 4;
    }

    qInfo() << "PAYLOAD-SELF-TEST PASS ListModel Unit ID 3 accepted";
    return 0;
}

int runIntegrationTest(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Leduo Lab"));
    QCoreApplication::setApplicationName(QStringLiteral("ART-Pi Gateway Studio Integration Test"));

    ArtPiClient client;
    client.setAutoRefresh(false);
    client.setEndpoint(valueAfterArgument(argc,
                                          argv,
                                          QByteArrayLiteral("--integration-test"),
                                          QStringLiteral("http://192.168.1.20")));

    QFile traceFile(QDir::temp().filePath(QStringLiteral("artpi_gateway_integration.log")));
    traceFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    const auto trace = [&](const QString &message) {
        QTextStream stream(&traceFile);
        stream << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz "))
               << message << Qt::endl;
    };
    trace(QStringLiteral("start endpoint=%1").arg(client.endpoint()));

    bool statusReady = false;
    bool configReady = false;
    bool writeStarted = false;
    const auto beginWriteWhenReady = [&]() {
        if (!statusReady || !configReady || writeStarted)
        {
            return;
        }
        writeStarted = true;
        const QVariantMap config = client.config();
        QVariantMap general{
            {QStringLiteral("rs485Role"), config.value(QStringLiteral("rs485_role"))},
            {QStringLiteral("modbusUnitId"), config.value(QStringLiteral("modbus_unit_id"))},
            {QStringLiteral("masterDeviceCount"), config.value(QStringLiteral("master_device_count"))},
            {QStringLiteral("offlineProbeSeconds"), config.value(QStringLiteral("offline_probe_s"))},
            {QStringLiteral("pollPeriodMs"), config.value(QStringLiteral("poll_period_ms"))},
            {QStringLiteral("redLedOn"), config.value(QStringLiteral("red_led_on"))},
            {QStringLiteral("buzzerOn"), config.value(QStringLiteral("buzzer_on"))}
        };
        QVariantList devices;
        const QVariantList sourceDevices = config.value(QStringLiteral("devices")).toList();
        for (const QVariant &entry : sourceDevices)
        {
            const QVariantMap source = entry.toMap();
            QVariantMap flattened{
                {QStringLiteral("unitId"), source.value(QStringLiteral("unit_id"))},
                {QStringLiteral("timeoutMs"), source.value(QStringLiteral("timeout_ms"))}
            };
            for (const QString &name : {QStringLiteral("coil"),
                                        QStringLiteral("discrete"),
                                        QStringLiteral("holding"),
                                        QStringLiteral("input")})
            {
                const QVariantMap range = source.value(name).toMap();
                flattened.insert(name + QStringLiteral("Address"),
                                 range.value(QStringLiteral("address")));
                flattened.insert(name + QStringLiteral("Quantity"),
                                 range.value(QStringLiteral("quantity")));
            }
            devices.append(flattened);
        }
        client.saveConfiguration(general, devices);
    };

    QObject::connect(&client, &ArtPiClient::statusUpdated, &application, [&]() {
        statusReady = client.connected() && client.devices()->rowCount() > 0;
        trace(QStringLiteral("status connected=%1 devices=%2 ready=%3")
                  .arg(client.connected())
                  .arg(client.devices()->rowCount())
                  .arg(statusReady));
        beginWriteWhenReady();
    });
    QObject::connect(&client, &ArtPiClient::configUpdated, &application, [&]() {
        configReady = client.config().value(QStringLiteral("ok")).toBool() &&
                      !client.config().value(QStringLiteral("devices")).toList().isEmpty();
        trace(QStringLiteral("config devices=%1 ready=%2")
                  .arg(client.config().value(QStringLiteral("devices")).toList().size())
                  .arg(configReady));
        beginWriteWhenReady();
    });
    QObject::connect(&client, &ArtPiClient::configurationSaved, &application, [&]() {
        trace(QStringLiteral("configuration saved"));
        const QVariantMap first = client.config().value(QStringLiteral("devices"))
                                      .toList().constFirst().toMap();
        const int address = first.value(QStringLiteral("coil")).toMap()
                                .value(QStringLiteral("address")).toInt();
        client.sendCommand(0, 0, address, 0);
    });
    QObject::connect(&client, &ArtPiClient::commandQueued, &application,
                     [&](qulonglong commandId) {
        trace(QStringLiteral("command queued id=%1").arg(commandId));
        qInfo().noquote() << QStringLiteral("INTEGRATION-TEST PASS command_id=%1")
                                 .arg(commandId);
        application.exit(0);
    });
    QObject::connect(&client, &ArtPiClient::commandCompleted, &application,
                     [&](bool ok, const QString &message) {
        trace(QStringLiteral("command completed ok=%1 message=%2").arg(ok).arg(message));
        if (!ok)
        {
            qCritical().noquote() << QStringLiteral("INTEGRATION-TEST FAIL %1").arg(message);
            application.exit(5);
        }
    });

    QTimer::singleShot(15000, &application, [&]() {
        trace(QStringLiteral("timeout busy=%1 error=%2").arg(client.busy()).arg(client.lastError()));
        qCritical().noquote() << QStringLiteral("INTEGRATION-TEST TIMEOUT error=%1")
                                     .arg(client.lastError());
        application.exit(6);
    });
    client.connectNow();
    return application.exec();
}

int runIndustrialSelfTest(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Leduo Lab"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("leduo.local"));
    QCoreApplication::setApplicationName(QStringLiteral("ART-Pi Gateway Studio Industrial Test"));
    QStandardPaths::setTestModeEnabled(true);

    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDirectory);
    const QString databasePath = QDir(dataDirectory).filePath(QStringLiteral("gateway_studio.db"));
    QFile::remove(databasePath);
    QFile::remove(databasePath + QStringLiteral("-wal"));
    QFile::remove(databasePath + QStringLiteral("-shm"));

    IndustrialManager manager;
    const int tagId = manager.addTag({
        {QStringLiteral("name"), QStringLiteral("Pressure")},
        {QStringLiteral("device_index"), 0},
        {QStringLiteral("register_class"), QStringLiteral("holding")},
        {QStringLiteral("value_index"), 0},
        {QStringLiteral("scale"), 0.1},
        {QStringLiteral("value_offset"), 0.0},
        {QStringLiteral("unit"), QStringLiteral("bar")},
        {QStringLiteral("high_enabled"), true},
        {QStringLiteral("high_limit"), 10.0},
        {QStringLiteral("low_enabled"), false},
        {QStringLiteral("enabled"), true}
    });

    const QVariantList values{
        QVariantList{0, 1},
        QVariantList{1, 0},
        QVariantList{125, 126},
        QVariantList{42, 43}
    };
    const QVariantMap snapshot{
        {QStringLiteral("board"), QStringLiteral("ART-Pi Test")},
        {QStringLiteral("ip"), QStringLiteral("192.0.2.1")},
        {QStringLiteral("firmware_version"), QStringLiteral("test")},
        {QStringLiteral("rs485"), QVariantMap{
             {QStringLiteral("devices"), QVariantList{
                  QVariantMap{{QStringLiteral("unit_id"), 1},
                              {QStringLiteral("state"), QStringLiteral("online")},
                              {QStringLiteral("values"), values}}
              }}
         }}
    };
    manager.ingestSnapshot(snapshot);
    manager.selectHistory(tagId, 1);

    const QVariantMap configuration{
        {QStringLiteral("ok"), true},
        {QStringLiteral("rs485_role"), 1},
        {QStringLiteral("master_device_count"), 1}
    };
    const int versionId = manager.saveConfigurationVersion(QStringLiteral("self-test"),
                                                            QStringLiteral("http://192.0.2.1"),
                                                            configuration);
    const int gatewayId = manager.addGateway(QStringLiteral("Test Gateway"),
                                             QStringLiteral("http://192.0.2.1"),
                                             QStringLiteral("self-test"));
    const bool securityOk = manager.initializeSecurity(QStringLiteral("industrial-test"));
    const bool userOk = manager.saveUser(QStringLiteral("operator1"),
                                         QStringLiteral("operator-test"),
                                         QStringLiteral("operator"), true);
    manager.logout();
    const bool loginOk = manager.login(QStringLiteral("admin"),
                                       QStringLiteral("industrial-test"));

    const QString backupPath = QDir::temp().filePath(QStringLiteral("artpi_gateway_self_test.db"));
    const QString reportPath = QDir::temp().filePath(QStringLiteral("artpi_gateway_self_test.csv"));
    QFile::remove(backupPath);
    QFile::remove(reportPath);
    const bool backupOk = manager.backupDatabase(backupPath);
    const bool reportOk = manager.exportReport(reportPath, 1);
    const bool outletOk = manager.configureOutlet({
        {QStringLiteral("enabled"), false},
        {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), 1883},
        {QStringLiteral("topic"), QStringLiteral("artpi/test")},
        {QStringLiteral("clientId"), QStringLiteral("artpi-self-test")},
        {QStringLiteral("intervalSeconds"), 10}
    });

    const bool passed = tagId > 0 && manager.tags().size() == 1 &&
                        !manager.history().isEmpty() && manager.activeAlarmCount() == 1 &&
                        manager.acknowledgeAllAlarms() == 1 && versionId > 0 &&
                        manager.restoreConfigurationVersion(versionId).value(QStringLiteral("ok")).toBool() &&
                        gatewayId > 0 && securityOk && userOk && loginOk &&
                        manager.users().size() == 2 && backupOk && reportOk && outletOk &&
                        QFileInfo(backupPath).size() > 0 && QFileInfo(reportPath).size() > 0;
    qInfo().noquote() << (passed ? QStringLiteral("INDUSTRIAL-SELF-TEST PASS")
                                : QStringLiteral("INDUSTRIAL-SELF-TEST FAIL"));
    return passed ? 0 : 7;
}

int runMqttSelfTest(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Leduo Lab"));
    QCoreApplication::setApplicationName(QStringLiteral("ART-Pi Gateway Studio MQTT Test"));
    QStandardPaths::setTestModeEnabled(true);

    // Make the loopback test repeatable even when a previous run persisted enabled=true.
    QSettings().setValue(QStringLiteral("outlet/enabled"), false);

    IndustrialManager manager;
    manager.ingestSnapshot({
        {QStringLiteral("board"), QStringLiteral("ART-Pi MQTT Test")},
        {QStringLiteral("ip"), QStringLiteral("192.0.2.2")},
        {QStringLiteral("firmware_version"), QStringLiteral("test")},
        {QStringLiteral("rs485"), QVariantMap{{QStringLiteral("devices"), QVariantList{}}}}
    });

    bool published = false;
    QObject::connect(&manager, &IndustrialManager::outletStateChanged,
                     &application, [&]() {
        if (manager.outletState() == QStringLiteral("online") && !published)
        {
            published = true;
            manager.publishCurrentSnapshot();
            QTimer::singleShot(300, &application, [&]() { application.exit(0); });
        }
    });
    manager.configureOutlet({
        {QStringLiteral("enabled"), true},
        {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), 18883},
        {QStringLiteral("topic"), QStringLiteral("artpi/self-test")},
        {QStringLiteral("clientId"), QStringLiteral("artpi-mqtt-self-test")},
        {QStringLiteral("username"), QString()},
        {QStringLiteral("password"), QString()},
        {QStringLiteral("intervalSeconds"), 10}
    });
    QTimer::singleShot(6000, &application, [&]() { application.exit(8); });
    return application.exec();
}
}

int main(int argc, char *argv[])
{
    if (hasArgument(argc, argv, QByteArrayLiteral("--self-test")))
    {
        return runSelfTest(argc, argv);
    }
    if (hasArgument(argc, argv, QByteArrayLiteral("--integration-test")))
    {
        return runIntegrationTest(argc, argv);
    }
    if (hasArgument(argc, argv, QByteArrayLiteral("--payload-self-test")))
    {
        return runPayloadSelfTest(argc, argv);
    }
    if (hasArgument(argc, argv, QByteArrayLiteral("--industrial-self-test")))
    {
        return runIndustrialSelfTest(argc, argv);
    }
    if (hasArgument(argc, argv, QByteArrayLiteral("--mqtt-self-test")))
    {
        return runMqttSelfTest(argc, argv);
    }

    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Leduo Lab"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("leduo.local"));
    QCoreApplication::setApplicationName(QStringLiteral("ART-Pi Gateway Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.4.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ArtPiClient client;
    IndustrialManager industrial;
    QObject::connect(&client, &ArtPiClient::statusUpdated, &industrial, [&]() {
        industrial.ingestSnapshot(client.statusSnapshot());
    });
    QObject::connect(&client, &ArtPiClient::logAdded, &industrial,
                     [&](const QString &level, const QString &message) {
        industrial.recordLog(level, QStringLiteral("gateway"), message);
    });
    QObject::connect(&client, &ArtPiClient::commandSubmitted, &industrial,
                     [&](int deviceIndex, int commandType, int address, int value) {
        industrial.recordAudit(QStringLiteral("modbus.command"),
                               QCoreApplication::translate("main", "设备 %1").arg(deviceIndex + 1),
                               QCoreApplication::translate("main", "类型 %1，地址 %2，值 %3")
                                   .arg(commandType).arg(address).arg(value),
                               true);
    });
    QObject::connect(&client, &ArtPiClient::configurationSaved, &industrial, [&]() {
        industrial.recordAudit(QStringLiteral("gateway.config"),
                               client.endpoint(),
                               QStringLiteral("configuration accepted"),
                               true);
    });
    QObject::connect(&industrial, &IndustrialManager::gatewayActivationRequested,
                     &client, [&](const QString &endpoint) {
        client.setEndpoint(endpoint);
        client.connectNow();
    });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("gateway"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("industrial"), &industrial);
    QObject::connect(&engine,
                     &QQmlApplicationEngine::objectCreationFailed,
                     &application,
                     []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("ArtPiGatewayStudio"), QStringLiteral("Main"));

    QObject *rootObject = engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().constFirst();
    if (rootObject != nullptr && hasArgument(argc, argv, QByteArrayLiteral("--page")))
    {
        rootObject->setProperty("currentPage",
                                valueAfterArgument(argc,
                                                   argv,
                                                   QByteArrayLiteral("--page"),
                                                    QStringLiteral("0")).toInt());
    }
    if (rootObject != nullptr && hasArgument(argc, argv, QByteArrayLiteral("--industrial-section")))
    {
        rootObject->setProperty("industrialSection",
                                valueAfterArgument(argc,
                                                   argv,
                                                   QByteArrayLiteral("--industrial-section"),
                                                   QStringLiteral("0")).toInt());
    }
    if (rootObject != nullptr && hasArgument(argc, argv, QByteArrayLiteral("--open-config")))
    {
        QTimer::singleShot(1800, rootObject, [rootObject]() {
            QMetaObject::invokeMethod(rootObject, "populateConfig");
        });
    }

    const bool uiSmokeTest = hasArgument(argc, argv, QByteArrayLiteral("--ui-smoke-test"));
    const QString screenshotPath = valueAfterArgument(argc,
                                                       argv,
                                                       QByteArrayLiteral("--screenshot"),
                                                       QString{});
    if (uiSmokeTest || !screenshotPath.isEmpty())
    {
        QTimer::singleShot(2800, &application, [&]() {
            if (!screenshotPath.isEmpty())
            {
                QQuickWindow *window = engine.rootObjects().isEmpty()
                    ? nullptr
                    : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
                if (window == nullptr)
                {
                    application.exit(3);
                    return;
                }
                const QFileInfo outputInfo(screenshotPath);
                QDir().mkpath(outputInfo.absolutePath());
                const QImage image = window->grabWindow();
                if (image.isNull() || !image.save(screenshotPath))
                {
                    application.exit(4);
                    return;
                }
            }
            application.exit(0);
        });
    }
    return application.exec();
}
