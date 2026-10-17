#include "app/sources/sourcedialog.h"

#include "app/editor/audiotransport.h"
#include "app/jobqueue.h"

#include <QAudioOutput>
#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

static QString duration(int s)
{
    if (s <= 0)
        return {};
    return s >= 3600 ? QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

static bool looksLikeLink(const QString &t)
{
    return t.startsWith("http://") || t.startsWith("https://") || t.startsWith("www.");
}

SourceDialog::SourceDialog(Mode mode, JobQueue *jobs, QWidget *parent)
    : QDialog(parent), m_mode(mode), m_jobs(jobs), m_net(new QNetworkAccessManager(this))
{
    setWindowTitle(mode == Stories ? tr("Ajouter depuis une source") : tr("Son depuis une source"));
    resize(900, 680);
    auto *lay = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_plugin = new QComboBox(this);
    m_plugins = PluginInfo::discover();
    for (const PluginInfo &p : std::as_const(m_plugins))
        m_plugin->addItem(p.icon.isEmpty() ? QIcon::fromTheme("folder-sound") : QIcon(p.icon), p.name);
    top->addWidget(new QLabel(tr("Source :"), this));
    top->addWidget(m_plugin, 1);
    lay->addLayout(top);
    m_about = new QLabel(this);
    m_about->setWordWrap(true);
    m_about->setStyleSheet("color: palette(placeholder-text);");
    lay->addWidget(m_about);

    auto *q = new QHBoxLayout;
    m_back = new QPushButton(QIcon::fromTheme("go-previous"), QString(), this);
    m_back->setToolTip(tr("Retour"));
    m_query = new QLineEdit(this);
    m_query->setClearButtonEnabled(true);
    m_go = new QPushButton(QIcon::fromTheme("edit-find"), tr("Chercher"), this);
    m_go->setDefault(true);
    q->addWidget(m_back);
    q->addWidget(m_query, 1);
    q->addWidget(m_go);
    lay->addLayout(q);
    m_where = new QLabel(this);
    lay->addWidget(m_where);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Titre"), tr("Durée"), tr("Date")});
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_list->setRootIsDecorated(false);
    m_list->setIconSize(QSize(56, 56));
    m_list->setUniformRowHeights(true);
    lay->addWidget(m_list, 1);

    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    m_details->setMaximumHeight(80);
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(m_details);

    auto *pv = new QHBoxLayout;
    m_preview = new QPushButton(QIcon::fromTheme("media-playback-start"), tr("Écouter"), this);
    m_transport = new AudioTransport(this);
    pv->addWidget(m_preview);
    pv->addWidget(m_transport, 1);
    lay->addLayout(pv);

    if (mode == Stories) {
        auto *g = new QHBoxLayout;
        m_grouping = new QButtonGroup(this);
        auto *one = new QRadioButton(tr("Une histoire par son"), this);
        auto *series = new QRadioButton(tr("Séries regroupées (épisodes d'une même série = parties)"), this);
        auto *all = new QRadioButton(tr("Une seule histoire :"), this);
        m_grouping->addButton(one, 0);
        m_grouping->addButton(series, 1);
        m_grouping->addButton(all, 2);
        one->setChecked(true);
        m_groupTitle = new QLineEdit(this);
        m_groupTitle->setPlaceholderText(tr("titre de l'histoire"));
        m_groupTitle->setEnabled(false);
        connect(all, &QRadioButton::toggled, m_groupTitle, &QWidget::setEnabled);
        g->addWidget(one);
        g->addWidget(series);
        g->addWidget(all);
        g->addWidget(m_groupTitle, 1);
        lay->addLayout(g);
    }

    auto *bottom = new QHBoxLayout;
    m_checkAll = new QPushButton(QIcon::fromTheme("edit-select-all"), tr("Tout cocher"), this);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);   // messages d'erreur des plugins parfois longs
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_add = bb->button(QDialogButtonBox::Ok);
    bottom->addWidget(m_checkAll);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(bb);
    lay->addLayout(bottom);
    m_checkAll->setVisible(mode == Stories);

    connect(bb, &QDialogButtonBox::accepted, this, &SourceDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_plugin, &QComboBox::currentIndexChanged, this, &SourceDialog::pluginChanged);
    connect(m_go, &QPushButton::clicked, this, &SourceDialog::go);
    connect(m_query, &QLineEdit::returnPressed, this, &SourceDialog::go);
    connect(m_query, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_go->setText(looksLikeLink(t.trimmed()) ? tr("Ouvrir") : tr("Chercher"));
    });
    connect(m_back, &QPushButton::clicked, this, [this]() {
        if (m_history.isEmpty())
            return;
        const auto prev = m_history.takeLast();
        m_items.clear();
        showItems(prev.second, prev.first);
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const int i = m_list->indexOfTopLevelItem(it);
        if (i < 0 || i >= m_items.size())
            return;
        if (m_items[i].isCollection())
            openCollection(m_items[i]);
        else if (m_mode == SingleSound) {
            it->setCheckState(0, Qt::Checked);
            accept();
        }
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, &SourceDialog::selectionChanged);
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int col) {
        if (col != 0 || m_mode != SingleSound || it->checkState(0) != Qt::Checked)
            return updateCount();
        // un seul son dans l'éditeur
        m_list->blockSignals(true);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (m_list->topLevelItem(i) != it && (m_list->topLevelItem(i)->flags() & Qt::ItemIsUserCheckable))
                m_list->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
        m_list->blockSignals(false);
        updateCount();
    });
    connect(m_checkAll, &QPushButton::clicked, this, [this]() {
        bool any = false;
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            any = any || ((m_list->topLevelItem(i)->flags() & Qt::ItemIsUserCheckable) && m_list->topLevelItem(i)->checkState(0) != Qt::Checked);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (m_list->topLevelItem(i)->flags() & Qt::ItemIsUserCheckable)
                m_list->topLevelItem(i)->setCheckState(0, any ? Qt::Checked : Qt::Unchecked);
    });
    connect(m_preview, &QPushButton::clicked, this, [this]() {
        const int i = m_list->indexOfTopLevelItem(m_list->currentItem());
        if (i < 0 || i >= m_items.size() || m_items[i].audioUrl.isEmpty())
            return;
        if (!m_player) {
            m_player = new QMediaPlayer(this);
            m_player->setAudioOutput(new QAudioOutput(this));
            m_transport->setPlayer(m_player);
        }
        m_player->setSource(QUrl(m_items[i].audioUrl));
        m_player->play();
    });

    if (m_plugins.isEmpty()) {
        m_about->setText(tr("Aucun plugin de source installé. Dossiers parcourus :\n%1").arg(PluginInfo::searchPaths().join("\n")));
        m_query->setEnabled(false);
        m_go->setEnabled(false);
    } else {
        const int last = m_plugin->findText(QSettings().value("sources/plugin").toString());
        m_plugin->setCurrentIndex(qMax(0, last));
        pluginChanged();
    }
    updateCount();
}

void SourceDialog::pluginChanged()
{
    const int i = m_plugin->currentIndex();
    if (i < 0 || i >= m_plugins.size())
        return;
    const PluginInfo &p = m_plugins[i];
    QSettings().setValue("sources/plugin", p.name);
    m_about->setText(p.description);
    QStringList hints;
    if (p.search)
        hints << (p.searchHint.isEmpty() ? tr("mots à chercher") : p.searchHint);
    if (p.link)
        hints << (p.linkHint.isEmpty() ? tr("lien") : p.linkHint);
    m_query->setPlaceholderText(hints.join(tr(" ou ")));
    m_history.clear();
    m_items.clear();
    showItems({}, {});
}

void SourceDialog::go()
{
    const QString t = m_query->text().trimmed();
    const int pi = m_plugin->currentIndex();
    if (t.isEmpty() || pi < 0)
        return;
    const PluginInfo p = m_plugins[pi];
    m_history.clear();
    if (looksLikeLink(t)) {
        if (!p.link)
            return void(QMessageBox::information(this, windowTitle(), tr("Cette source n'accepte pas de lien.")));
        runQuery([t](PluginRunner &r, QString *title) { return r.open(t, title); }, tr("Ouverture du lien…"));
    } else {
        if (!p.search)
            return void(QMessageBox::information(this, windowTitle(), tr("Cette source ne fait pas de recherche : collez un lien.")));
        runQuery([t](PluginRunner &r, QString *title) {
            *title = QObject::tr("Résultats pour « %1 »").arg(t);
            return r.search(t);
        }, tr("Recherche…"));
    }
}

void SourceDialog::openCollection(const SourceItem &item)
{
    const QString url = item.url.isEmpty() ? item.id : item.url;
    m_history << qMakePair(m_where->text(), m_items);
    m_collectionImage = item.image;
    runQuery([url](PluginRunner &r, QString *title) { return r.open(url, title); }, tr("Ouverture de « %1 »…").arg(item.title));
}

void SourceDialog::runQuery(const std::function<QList<SourceItem>(PluginRunner &, QString *)> &query, const QString &busy)
{
    const PluginInfo p = m_plugins[m_plugin->currentIndex()];
    const int gen = ++m_generation;
    m_status->setText(busy);
    m_go->setEnabled(false);
    setCursor(Qt::BusyCursor);
    struct Answer { QList<SourceItem> items; QString title, error; };
    auto *w = new QFutureWatcher<Answer>(this);
    connect(w, &QFutureWatcher<Answer>::finished, this, [this, w, gen]() {
        const Answer a = w->result();
        w->deleteLater();
        if (gen != m_generation)
            return;
        unsetCursor();
        m_go->setEnabled(true);
        if (!a.error.isEmpty()) {
            if (!m_history.isEmpty())
                m_history.removeLast();
            m_status->setText(tr("Erreur : %1").arg(a.error));
            return;
        }
        showItems(a.items, a.title);
    });
    w->setFuture(QtConcurrent::run([p, query]() {
        PluginRunner r(p);
        Answer a;
        a.items = query(r, &a.title);
        a.error = r.error();
        return a;
    }));
}

void SourceDialog::showItems(const QList<SourceItem> &items, const QString &title)
{
    m_items = items;
    m_collectionTitle = title;
    m_where->setText(title.isEmpty() ? QString() : QString("<b>%1</b>").arg(title.toHtmlEscaped()));
    m_back->setEnabled(!m_history.isEmpty());
    if (m_groupTitle && m_groupTitle->text().isEmpty())
        m_groupTitle->setText(title);
    m_list->blockSignals(true);
    m_list->clear();
    for (const SourceItem &s : items) {
        auto *it = new QTreeWidgetItem(m_list);
        QString text = s.title;
        if (!s.subtitle.isEmpty())
            text += "\n" + s.subtitle;
        it->setText(0, text);
        it->setText(1, duration(s.duration));
        it->setText(2, s.date.left(10));
        it->setToolTip(0, s.description.left(600));
        if (s.isCollection()) {
            it->setIcon(0, QIcon::fromTheme("folder-sound"));
            it->setToolTip(0, tr("Double-clic pour ouvrir") + (s.description.isEmpty() ? QString() : "\n\n" + s.description.left(600)));
        } else {
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, Qt::Unchecked);
            it->setIcon(0, QIcon::fromTheme("audio-x-generic"));
        }
        if (!s.image.isEmpty())
            loadThumb(it, s.image);
    }
    m_list->blockSignals(false);
    m_status->setText(items.isEmpty() && !title.isEmpty() ? tr("Aucun résultat") : QString());
    selectionChanged();
    updateCount();
}

void SourceDialog::loadThumb(QTreeWidgetItem *it, const QString &url)
{
    if (QFileInfo(url).isFile()) {
        it->setIcon(0, QIcon(QPixmap(url).scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        return;
    }
    QNetworkReply *r = m_net->get(QNetworkRequest(QUrl(url)));
    const int gen = m_generation;
    connect(r, &QNetworkReply::finished, this, [this, r, it, gen]() {
        r->deleteLater();
        if (gen != m_generation || r->error() != QNetworkReply::NoError)
            return;
        QPixmap px;
        if (px.loadFromData(r->readAll()))
            it->setIcon(0, QIcon(px.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    });
}

void SourceDialog::selectionChanged()
{
    const int i = m_list->indexOfTopLevelItem(m_list->currentItem());
    if (i < 0 || i >= m_items.size()) {
        m_details->clear();
        m_preview->setEnabled(false);
        return;
    }
    const SourceItem &s = m_items[i];
    m_details->setText(s.description.left(400));
    m_preview->setEnabled(!s.audioUrl.isEmpty());
    m_preview->setToolTip(s.audioUrl.isEmpty() ? tr("Écoute indisponible pour cette source avant téléchargement") : QString());
}

QList<SourceItem> SourceDialog::checkedItems() const
{
    QList<SourceItem> out;
    for (int i = 0; i < m_list->topLevelItemCount() && i < m_items.size(); ++i)
        if (m_list->topLevelItem(i)->checkState(0) == Qt::Checked && !m_items[i].isCollection())
            out << m_items[i];
    return out;
}

void SourceDialog::updateCount()
{
    const int n = int(checkedItems().size());
    m_add->setEnabled(n > 0);
    m_add->setText(m_mode == SingleSound ? tr("Utiliser ce son") : tr("Ajouter (%1)").arg(n));
}

void SourceDialog::accept()
{
    const QList<SourceItem> items = checkedItems();
    if (items.isEmpty() || !m_jobs)
        return;
    const PluginInfo p = m_plugins[m_plugin->currentIndex()];
    if (m_player)
        m_player->stop();

    // téléchargements dans la file des transferts, un par son
    struct Shared {
        QList<FetchedAudio> got;
        QStringList errors;
        QAtomicInt cancel;
        int finished = 0;
    };
    auto sh = std::make_shared<Shared>();
    sh->got.resize(items.size());
    QPointer<JobQueue> jobs = m_jobs;
    QList<int> ids;
    QProgressDialog progress(tr("Téléchargement…"), tr("Annuler"), 0, int(items.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    QEventLoop loop;
    for (int k = 0; k < items.size(); ++k) {
        const SourceItem it = items[k];
        ids << m_jobs->enqueue(
            tr("Téléchargement de « %1 »").arg(it.title),
            [p, it, sh, k, jobs]() {
                PluginRunner r(p);
                const FetchedAudio f = r.fetch(it, [jobs](double frac, const QString &msg) {
                    if (jobs)
                        jobs->reportProgress(frac, msg);
                }, &sh->cancel);
                sh->got[k] = f;
                if (f.audio.isEmpty())
                    sh->errors << QString("%1 : %2").arg(it.title, r.error());
                return !f.audio.isEmpty();
            },
            [sh, &progress, &loop, n = int(items.size()), title = it.title](bool) {
                ++sh->finished;
                progress.setValue(sh->finished);
                progress.setLabelText(QObject::tr("Téléchargé : %1").arg(title));
                if (sh->finished >= n)
                    loop.quit();
            });
    }
    connect(&progress, &QProgressDialog::canceled, this, [&, sh]() {
        sh->cancel.storeRelaxed(1);
        for (int id : std::as_const(ids))
            if (m_jobs && m_jobs->removePending(id))
                ++sh->finished;
        if (sh->finished >= int(items.size()))
            loop.quit();
    });
    if (sh->finished < int(items.size()))
        loop.exec();
    progress.disconnect(this);   // fermer la fenêtre émet « canceled » : ce n'est pas une annulation
    progress.close();
    if (sh->cancel.loadRelaxed())
        return;   // la fenêtre reste ouverte, les fichiers déjà reçus sont en cache
    if (!sh->errors.isEmpty())
        QMessageBox::warning(this, windowTitle(), tr("Certains sons n'ont pas pu être récupérés :\n%1").arg(sh->errors.join("\n")));

    // regroupement en histoires
    m_result.clear();
    const int grouping = m_grouping ? m_grouping->checkedId() : 0;
    QMap<QString, int> series;   // titre de série -> index dans m_result
    for (int k = 0; k < items.size(); ++k) {
        const FetchedAudio &f = sh->got[k];
        if (f.audio.isEmpty())
            continue;
        const QString title = f.title.isEmpty() ? items[k].title : f.title;
        if (m_mode == SingleSound || grouping == 0) {
            m_result << Story{title, f.image, {f.audio}};
        } else if (grouping == 2) {
            if (m_result.isEmpty())
                m_result << Story{m_groupTitle->text().trimmed().isEmpty() ? m_collectionTitle : m_groupTitle->text().trimmed(), f.image, {}};
            m_result.first().parts << f.audio;
        } else {
            const QString key = items[k].groupTitle.isEmpty() ? title : items[k].groupTitle;
            if (!series.contains(key)) {
                series.insert(key, int(m_result.size()));
                m_result << Story{key, f.image, {}};
            }
            m_result[series[key]].parts << f.audio;
        }
    }
    // séries : parties dans l'ordre des numéros annoncés par le plugin
    if (grouping == 1) {
        for (Story &s : m_result) {
            QList<QPair<int, QString>> ordered;
            for (int k = 0; k < items.size(); ++k)
                if (s.parts.contains(sh->got[k].audio))
                    ordered << qMakePair(items[k].groupPart ? items[k].groupPart : 1000 + k, sh->got[k].audio);
            std::stable_sort(ordered.begin(), ordered.end(), [](auto &a, auto &b) { return a.first < b.first; });
            s.parts.clear();
            for (const auto &o : ordered)
                s.parts << o.second;
        }
    }
    if (!m_result.isEmpty())
        QDialog::accept();
}
