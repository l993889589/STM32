#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>

class DeviceListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        IndexRole = Qt::UserRole + 1,
        UnitIdRole,
        StateRole,
        ConsecutiveFailuresRole,
        BackoffStepRole,
        LastFunctionRole,
        LastExceptionRole,
        SuccessfulPollsRole,
        TimeoutsRole,
        ProtocolErrorsRole,
        LastSuccessMsRole,
        NextActionMsRole,
        ValuesRole,
        CoilValuesRole,
        DiscreteValuesRole,
        HoldingValuesRole,
        InputValuesRole
    };

    explicit DeviceListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void updateFromVariantList(const QVariantList &devices);
    void clear();

private:
    static QVariant valuesAt(const QVariantMap &device, int index);

    QVector<QVariantMap> m_devices;
};
