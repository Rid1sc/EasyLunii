#include "app/jobqueue.h"

#include <QFutureWatcher>
#include <QtConcurrent>

JobQueue::JobQueue(QObject *parent) : QObject(parent) {}

void JobQueue::enqueue(const QString &label, std::function<bool()> run, std::function<void(bool)> done)
{
    m_queue.enqueue({label, std::move(run), std::move(done)});
    if (!m_running)
        next();
}

void JobQueue::clearPending()
{
    m_queue.clear();
}

void JobQueue::next()
{
    if (m_queue.isEmpty()) {
        m_running = false;
        emit idle();
        return;
    }
    m_running = true;
    Job job = m_queue.dequeue();
    emit started(job.label, int(m_queue.size()));
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, job]() {
        const bool ok = w->result();
        w->deleteLater();
        if (job.done)
            job.done(ok);
        emit finished(job.label, ok);
        next();
    });
    w->setFuture(QtConcurrent::run(job.run));
}
