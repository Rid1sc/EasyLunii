// File de tâches exécutées une par une dans un thread de travail (transferts, exports…).
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QQueue>
#include <functional>

class JobQueue : public QObject
{
    Q_OBJECT
public:
    struct Job {
        int id = 0;
        QString label;
        std::function<bool()> run;           // exécuté hors du thread de l'interface
        std::function<void(bool)> done;      // rappelé dans le thread de l'interface
    };
    struct Finished {
        QString label;
        bool ok = false;
        QDateTime at;
    };

    explicit JobQueue(QObject *parent = nullptr);
    void enqueue(const QString &label, std::function<bool()> run, std::function<void(bool)> done = {});
    bool isBusy() const { return m_running || !m_queue.isEmpty(); }
    int pending() const { return int(m_queue.size()); }
    void clearPending();
    bool removePending(int id);   // retire une tâche pas encore commencée

    // état, pour l'affichage
    QString currentLabel() const { return m_running ? m_current : QString(); }
    QList<QPair<int, QString>> pendingJobs() const;
    const QList<Finished> &history() const { return m_history; }   // plus récentes en premier
    void clearHistory();

signals:
    void started(const QString &label, int remaining);
    void finished(const QString &label, bool ok);
    void idle();
    void changed();   // file, tâche en cours ou historique modifiés

private:
    void next();
    QQueue<Job> m_queue;
    bool m_running = false;
    QString m_current;
    int m_nextId = 1;
    QList<Finished> m_history;
};
