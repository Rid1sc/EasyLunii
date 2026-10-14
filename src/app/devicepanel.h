// Panneau de la Lunii : sélection, montage/éjection, liste des histoires (ordre, glisser-déposer).
#pragma once

#include "app/devicewatcher.h"
#include "core/storydevice.h"

#include <QListWidget>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QWidget>

class JobQueue;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;

// Liste des histoires : déplacement interne + dépôt d'archives (bibliothèque ou fichiers)
class DeviceList : public QListWidget
{
    Q_OBJECT
public:
    explicit DeviceList(QWidget *parent = nullptr);

signals:
    void archivesDropped(const QStringList &paths, int row);
    void orderChanged(const QList<QUuid> &order);

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

class DevicePanel : public QWidget
{
    Q_OBJECT
public:
    DevicePanel(JobQueue *jobs, QWidget *parent = nullptr);

    lunii::StoryDevice *device() const { return m_device; }
    QSet<QString> storyUuids() const;   // UUID canoniques présents sur la Lunii
    bool hasDevice() const { return m_device != nullptr; }

    void sendToDevice(const QStringList &archives);
    void exportStories(const QList<QUuid> &uuids, const QString &dir, const std::function<void()> &done = {});
    QList<QUuid> selectedStories() const;
    void openPath(const QString &mountPoint);   // Lunii montée ailleurs (dossier de test)
    // Exécute une opération sur la Lunii dans la file de tâches, puis recharge la liste
    void runOnDevice(const QString &label, std::function<bool(lunii::StoryDevice *)> op);

signals:
    void deviceChanged();
    void storiesChanged();
    void logMessage(int level, const QString &message);
    void progress(const QString &label, int current, int total);

public slots:
    void reloadStories();
    void recover();
    void cleanup();
    void nightModeSettings();

private:
    void onDevicesChanged();
    void selectDevice(int index);
    void updateInfo();
    void updateButtons();
    void moveSelection(int delta);
    void loadCovers();

    JobQueue *m_jobs;
    DeviceWatcher m_watcher;
    QPointer<lunii::StoryDevice> m_device;
    QString m_block;
    QComboBox *m_combo;
    QPushButton *m_mountBtn, *m_ejectBtn;
    QLabel *m_info;
    QProgressBar *m_space;
    DeviceList *m_list;
    QToolButton *m_up, *m_down, *m_hide, *m_night, *m_export, *m_remove;
    bool m_busy = false;
    int m_coverGeneration = 0;
};
