#include "app/jobqueue.h"

#include <QFutureWatcher>
#include <QtConcurrent>

static constexpr int HISTORY_MAX = 50;

JobQueue::JobQueue(QObject *parent) : QObject(parent) {}

int JobQueue::enqueue(const QString &label, std::function<bool()> run, std::function<void(bool)> done)
{
    const int id = m_nextId++;
    m_queue.enqueue({id, label, std::move(run), std::move(done)});
    emit changed();
    if (!m_running)
        next();
    return id;
}

void JobQueue::reportProgress(double fraction, const QString &detail)
{
    emit progressed(fraction, detail);   // connexion en file d'attente vers l'interface
}

void JobQueue::clearPending()
{
    // tâches abandonnées sans rappel : la tâche en cours signale la fin de la file (pending() == 0)
    m_queue.clear();
    emit changed();
}

bool JobQueue::removePending(int id)
{
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id == id) {
            m_queue.removeAt(i);
            emit changed();
            return true;
        }
    }
    return false;
}

QList<QPair<int, QString>> JobQueue::pendingJobs() const
{
    QList<QPair<int, QString>> out;
    for (const Job &j : m_queue)
        out << qMakePair(j.id, j.label);
    return out;
}

void JobQueue::clearHistory()
{
    m_history.clear();
    emit changed();
}

void JobQueue::next()
{
    if (m_queue.isEmpty()) {
        m_running = false;
        m_current.clear();
        emit changed();
        emit idle();
        return;
    }
    m_running = true;
    Job job = m_queue.dequeue();
    m_current = job.label;
    emit started(job.label, int(m_queue.size()));
    emit changed();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, job]() {
        const bool ok = w->result();
        w->deleteLater();
        m_history.prepend({job.label, ok, QDateTime::currentDateTime()});
        while (m_history.size() > HISTORY_MAX)
            m_history.removeLast();
        if (job.done)
            job.done(ok);
        emit finished(job.label, ok);
        next();
    });
    w->setFuture(QtConcurrent::run(job.run));
}
