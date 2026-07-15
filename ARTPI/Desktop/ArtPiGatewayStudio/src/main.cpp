#include "artpi_client.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
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

    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Leduo Lab"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("leduo.local"));
    QCoreApplication::setApplicationName(QStringLiteral("ART-Pi Gateway Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ArtPiClient client;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("gateway"), &client);
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
