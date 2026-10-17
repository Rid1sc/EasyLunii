// Panneau « Transferts » : tâche en cours avec sa progression, file d'attente (retirable) et tâches terminées.
#pragma once

#include <QWidget>

class JobQueue;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

class TransferPanel : public QWidget
{
    Q_OBJECT
public:
    TransferPanel(JobQueue *jobs, QWidget *parent = nullptr);

public slots:
    void setProgress(int current, int total);   // fichiers de l'histoire en cours
    void setDetail(const QString &text);        // débit…
    void setFraction(double fraction, const QString &detail);   // téléchargements (0 à 1)

signals:
    void abortCurrent();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void refresh();
    void removeSelected();

    JobQueue *m_jobs;
    QLabel *m_current, *m_detail, *m_pendingTitle;
    QProgressBar *m_bar;
    QPushButton *m_stop, *m_remove, *m_clearPending, *m_clearDone;
    QListWidget *m_pending, *m_done;
};
