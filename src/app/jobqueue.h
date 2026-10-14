// File de tâches exécutées une par une dans un thread de travail (transferts, exports…).
#pragma once

#include <QAtomicInt>
#include <QObject>
#include <QQueue>
#include <functional>

class JobQueue : public QObject
{
    Q_OBJECT
public:
    struct Job {
        QString label;
        std::function<bool()> run;           // exécuté hors du thread de l'interface
        std::function<void(bool)> done;      // rappelé dans le thread de l'interface
    };

    explicit JobQueue(QObject *parent = nullptr);
    void enqueue(const QString &label, std::function<bool()> run, std::function<void(bool)> done = {});
    bool isBusy() const { return m_running || !m_queue.isEmpty(); }
    int pending() const { return int(m_queue.size()); }
    void clearPending();

signals:
    void started(const QString &label, int remaining);
    void finished(const QString &label, bool ok);
    void idle();

private:
    void next();
    QQueue<Job> m_queue;
    bool m_running = false;
};
