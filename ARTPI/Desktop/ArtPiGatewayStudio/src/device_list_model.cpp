#include "device_list_model.h"

#include <algorithm>

DeviceListModel::DeviceListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_devices.size());
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size())
    {
        return {};
    }

    const QVariantMap &device = m_devices.at(index.row());
    switch (role)
    {
    case IndexRole:
        return device.value(QStringLiteral("index"));
    case UnitIdRole:
        return device.value(QStringLiteral("unit_id"));
    case StateRole:
        return device.value(QStringLiteral("state"));
    case ConsecutiveFailuresRole:
        return device.value(QStringLiteral("consecutive_failures"));
    case BackoffStepRole:
        return device.value(QStringLiteral("backoff_step"));
    case LastFunctionRole:
        return device.value(QStringLiteral("last_function"));
    case LastExceptionRole:
        return device.value(QStringLiteral("last_exception"));
    case SuccessfulPollsRole:
        return device.value(QStringLiteral("successful_polls"));
    case TimeoutsRole:
        return device.value(QStringLiteral("timeouts"));
    case ProtocolErrorsRole:
        return device.value(QStringLiteral("protocol_errors"));
    case LastSuccessMsRole:
        return device.value(QStringLiteral("last_success_ms"));
    case NextActionMsRole:
        return device.value(QStringLiteral("next_action_ms"));
    case ValuesRole:
        return device.value(QStringLiteral("values"));
    case CoilValuesRole:
        return valuesAt(device, 0);
    case DiscreteValuesRole:
        return valuesAt(device, 1);
    case HoldingValuesRole:
        return valuesAt(device, 2);
    case InputValuesRole:
        return valuesAt(device, 3);
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return {
        {IndexRole, "deviceIndex"},
        {UnitIdRole, "unitId"},
        {StateRole, "deviceState"},
        {ConsecutiveFailuresRole, "consecutiveFailures"},
        {BackoffStepRole, "backoffStep"},
        {LastFunctionRole, "lastFunction"},
        {LastExceptionRole, "lastException"},
        {SuccessfulPollsRole, "successfulPolls"},
        {TimeoutsRole, "timeoutCount"},
        {ProtocolErrorsRole, "protocolErrors"},
        {LastSuccessMsRole, "lastSuccessMs"},
        {NextActionMsRole, "nextActionMs"},
        {ValuesRole, "registerValues"},
        {CoilValuesRole, "coilValues"},
        {DiscreteValuesRole, "discreteValues"},
        {HoldingValuesRole, "holdingValues"},
        {InputValuesRole, "inputValues"}
    };
}

void DeviceListModel::updateFromVariantList(const QVariantList &devices)
{
    QVector<QVariantMap> replacement;
    replacement.reserve(devices.size());
    std::transform(devices.cbegin(), devices.cend(), std::back_inserter(replacement),
                   [](const QVariant &entry) { return entry.toMap(); });

    if (replacement.size() != m_devices.size())
    {
        beginResetModel();
        m_devices = std::move(replacement);
        endResetModel();
        return;
    }

    if (replacement.isEmpty())
    {
        return;
    }

    m_devices = std::move(replacement);
    emit dataChanged(index(0, 0), index(m_devices.size() - 1, 0));
}

void DeviceListModel::clear()
{
    if (m_devices.isEmpty())
    {
        return;
    }
    beginResetModel();
    m_devices.clear();
    endResetModel();
}

QVariant DeviceListModel::valuesAt(const QVariantMap &device, int index)
{
    const QVariantList classes = device.value(QStringLiteral("values")).toList();
    return (index >= 0 && index < classes.size()) ? classes.at(index) : QVariantList{};
}
