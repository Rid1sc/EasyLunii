// Détection des Lunii branchées (montées ou non) et montage/éjection via udisksctl.
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

struct DetectedDevice {
    QString blockDevice;   // /dev/sdc1 (ou /dev/sdc si pas de partition)
    QString mountPoint;    // vide si non montée
    QString model;         // "Lunii v1", "Lunii v2/v3"…
    bool operator==(const DetectedDevice &o) const
    {
        return blockDevice == o.blockDevice && mountPoint == o.mountPoint;
    }
};

class DeviceWatcher : public QObject
{
    Q_OBJECT
public:
    explicit DeviceWatcher(QObject *parent = nullptr);
    const QList<DetectedDevice> &devices() const { return m_devices; }

    static bool mount(const QString &block, QString *mountPoint, QString *error);
    static bool eject(const QString &block, QString *error);   // démonte puis met hors tension

public slots:
    void refresh();

signals:
    void devicesChanged();

private:
    QTimer m_timer;
    QList<DetectedDevice> m_devices;
};
