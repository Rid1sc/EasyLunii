#include "app/transferpanel.h"

#include "app/jobqueue.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

TransferPanel::TransferPanel(JobQueue *jobs, QWidget *parent) : QWidget(parent), m_jobs(jobs)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *curRow = new QHBoxLayout;
    m_current = new QLabel(this);
    m_current->setWordWrap(true);
    m_stop = new QPushButton(QIcon::fromTheme("process-stop"), tr("Arrêter"), this);
    m_stop->setToolTip(tr("Interrompre la tâche en cours (les suivantes continuent)"));
    curRow->addWidget(m_current, 1);
    curRow->addWidget(m_stop);
    lay->addLayout(curRow);
    m_bar = new QProgressBar(this);
    m_bar->setMaximumHeight(16);
    lay->addWidget(m_bar);
    m_detail = new QLabel(this);
    m_detail->setStyleSheet("color: palette(placeholder-text);");
    lay->addWidget(m_detail);

    m_pendingTitle = new QLabel(this);
    lay->addWidget(m_pendingTitle);
    m_pending = new QListWidget(this);
    m_pending->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pending->installEventFilter(this);
    lay->addWidget(m_pending, 2);
    auto *pRow = new QHBoxLayout;
    m_remove = new QPushButton(QIcon::fromTheme("list-remove"), tr("Retirer"), this);
    m_remove->setToolTip(tr("Retirer les tâches sélectionnées de la file (touche Suppr)"));
    m_clearPending = new QPushButton(QIcon::fromTheme("edit-clear"), tr("Vider la file"), this);
    pRow->addWidget(m_remove);
    pRow->addWidget(m_clearPending);
    pRow->addStretch();
    lay->addLayout(pRow);

    lay->addWidget(new QLabel(tr("<b>Terminés</b>"), this));
    m_done = new QListWidget(this);
    lay->addWidget(m_done, 1);
    m_clearDone = new QPushButton(QIcon::fromTheme("edit-clear"), tr("Effacer la liste"), this);
    auto *dRow = new QHBoxLayout;
    dRow->addWidget(m_clearDone);
    dRow->addStretch();
    lay->addLayout(dRow);

    connect(m_jobs, &JobQueue::changed, this, &TransferPanel::refresh);
    connect(m_jobs, &JobQueue::started, this, [this]() {
        m_bar->setRange(0, 0);   // indéterminé jusqu'à la première progression
        m_detail->clear();
    });
    connect(m_stop, &QPushButton::clicked, this, &TransferPanel::abortCurrent);
    connect(m_remove, &QPushButton::clicked, this, &TransferPanel::removeSelected);
    connect(m_clearPending, &QPushButton::clicked, m_jobs, &JobQueue::clearPending);
    connect(m_clearDone, &QPushButton::clicked, m_jobs, &JobQueue::clearHistory);
    connect(m_pending, &QListWidget::itemSelectionChanged, this,
            [this]() { m_remove->setEnabled(!m_pending->selectedItems().isEmpty()); });
    refresh();
}

void TransferPanel::setProgress(int current, int total)
{
    if (total > 0) {
        m_bar->setRange(0, total);
        m_bar->setValue(qMin(current + 1, total));
        m_bar->setFormat(tr("fichier %v / %m"));
    }
}

void TransferPanel::setDetail(const QString &text)
{
    m_detail->setText(text);
}

void TransferPanel::removeSelected()
{
    for (QListWidgetItem *it : m_pending->selectedItems())
        m_jobs->removePending(it->data(Qt::UserRole).toInt());
}

bool TransferPanel::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_pending && e->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Delete) {
        removeSelected();
        return true;
    }
    return QWidget::eventFilter(o, e);
}

void TransferPanel::refresh()
{
    const QString cur = m_jobs->currentLabel();
    m_current->setText(cur.isEmpty() ? tr("Aucun transfert en cours") : tr("<b>En cours :</b> %1").arg(cur.toHtmlEscaped()));
    m_bar->setVisible(!cur.isEmpty());
    m_detail->setVisible(!cur.isEmpty());
    m_stop->setEnabled(!cur.isEmpty());

    const auto pending = m_jobs->pendingJobs();
    m_pendingTitle->setText(tr("<b>En attente (%1)</b>").arg(pending.size()));
    QList<int> selected;
    for (QListWidgetItem *it : m_pending->selectedItems())
        selected << it->data(Qt::UserRole).toInt();
    m_pending->clear();
    int n = 0;
    for (const auto &[id, label] : pending) {
        auto *it = new QListWidgetItem(QIcon::fromTheme("media-playlist-append"), QString("%1. %2").arg(++n).arg(label), m_pending);
        it->setData(Qt::UserRole, id);
        it->setSelected(selected.contains(id));
    }
    m_clearPending->setEnabled(!pending.isEmpty());
    m_remove->setEnabled(!m_pending->selectedItems().isEmpty());

    m_done->clear();
    for (const auto &f : m_jobs->history())
        new QListWidgetItem(QIcon::fromTheme(f.ok ? "dialog-ok-apply" : "dialog-error"),
                            QString("%1  %2").arg(f.at.toString("HH:mm:ss"), f.label), m_done);
    m_clearDone->setEnabled(!m_jobs->history().isEmpty());
}
