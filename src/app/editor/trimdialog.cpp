#include "app/editor/trimdialog.h"

#include "app/editor/audiotransport.h"
#include "core/convert.h"

#include <QAudioOutput>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

static constexpr double ENV_RATE = 100;   // valeurs d'enveloppe par seconde

static QString stamp(double t)
{
    if (t < 0)
        return QStringLiteral("–");
    const int ds = int(std::lround(t * 10));
    return QString("%1:%2.%3").arg(ds / 600).arg(ds / 10 % 60, 2, 10, QChar('0')).arg(ds % 10);
}

// ---------------------------------------------------------------- forme d'onde

WaveformView::WaveformView(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(160);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::IBeamCursor);
}

void WaveformView::setEnvelope(const QVector<float> &env, double perSecond)
{
    m_env = env;
    m_perSecond = perSecond;
    m_viewStart = 0;
    m_viewLen = duration();
    update();
    emit viewChanged();
}

void WaveformView::setSelection(const Span &s)
{
    m_sel = s;
    update();
    emit selectionChanged(m_sel);
}

void WaveformView::setRemoved(const QList<Span> &r)
{
    m_removed = r;
    update();
}

void WaveformView::setPlayhead(double t)
{
    m_playhead = t;
    // suivi de la lecture quand on est zoomé
    if (t >= 0 && m_viewLen < duration() && (t < m_viewStart || t > m_viewStart + m_viewLen))
        setView(t - m_viewLen * 0.1, m_viewLen);
    update();
}

void WaveformView::setView(double start, double length)
{
    const double d = duration();
    m_viewLen = qBound(qMin(2.0, d), length, d);
    m_viewStart = qBound(0.0, start, qMax(0.0, d - m_viewLen));
    update();
    emit viewChanged();
}

void WaveformView::zoom(double factor, double around)
{
    const double newLen = m_viewLen / factor;
    const double rel = m_viewLen > 0 ? (around - m_viewStart) / m_viewLen : 0.5;
    setView(around - rel * newLen, newLen);
}

double WaveformView::timeAt(double x) const
{
    return m_viewStart + qBound(0.0, x / qMax(1, width()), 1.0) * m_viewLen;
}

double WaveformView::xAt(double t) const
{
    return m_viewLen > 0 ? (t - m_viewStart) / m_viewLen * width() : 0;
}

void WaveformView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));
    if (m_env.isEmpty() || m_viewLen <= 0) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Analyse du son…"));
        return;
    }
    const int w = width(), h = height(), mid = h / 2;
    // enveloppe : maximum des valeurs couvertes par chaque colonne
    const QColor wave = pal.color(QPalette::Highlight);
    const QColor gone = pal.color(QPalette::PlaceholderText);
    for (int x = 0; x < w; ++x) {
        const double t0 = timeAt(x), t1 = timeAt(x + 1);
        int i0 = int(t0 * m_perSecond), i1 = qMax(i0 + 1, int(t1 * m_perSecond));
        float v = 0;
        for (int i = qMax(0, i0); i < qMin(i1, int(m_env.size())); ++i)
            v = qMax(v, m_env[i]);
        bool removed = false;
        for (const Span &r : m_removed)
            removed = removed || (t0 >= r.first && t0 < r.second);
        p.setPen(removed ? gone : wave);
        const int a = int(v * (mid - 4));
        p.drawLine(x, mid - a, x, mid + a);
    }
    // passages retirés : hachures
    for (const Span &r : m_removed) {
        const QRectF rr(xAt(r.first), 0, xAt(r.second) - xAt(r.first), h);
        if (rr.right() < 0 || rr.left() > w)
            continue;
        QColor c = pal.color(QPalette::Text);
        c.setAlpha(40);
        p.fillRect(rr, QBrush(c, Qt::BDiagPattern));
    }
    // sélection, par-dessus la forme d'onde
    if (m_sel.first >= 0 && m_sel.second > m_sel.first) {
        const QRectF sr(xAt(m_sel.first), 0, xAt(m_sel.second) - xAt(m_sel.first), h);
        QColor c = pal.color(QPalette::HighlightedText);
        c.setAlpha(90);
        p.fillRect(sr, c);
        p.setPen(QPen(pal.color(QPalette::HighlightedText), 2));
        p.drawLine(sr.topLeft(), sr.bottomLeft());
        p.drawLine(sr.topRight(), sr.bottomRight());
    }
    // graduations
    p.setPen(pal.color(QPalette::PlaceholderText));
    const double steps[] = {0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    double step = 600;
    for (double s : steps)
        if (m_viewLen / s <= 12) {
            step = s;
            break;
        }
    for (double t = std::ceil(m_viewStart / step) * step; t <= m_viewStart + m_viewLen; t += step) {
        const int x = int(xAt(t));
        p.drawLine(x, h - 6, x, h);
        p.drawText(x + 3, h - 4, stamp(t));
    }
    // tête de lecture
    if (m_playhead >= 0) {
        p.setPen(QPen(pal.color(QPalette::Text), 1));
        p.drawLine(QPointF(xAt(m_playhead), 0), QPointF(xAt(m_playhead), h));
    }
}

void WaveformView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_dragFrom = timeAt(e->position().x());
    m_sel = {m_dragFrom, m_dragFrom};
    update();
}

void WaveformView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragFrom < 0)
        return;
    const double t = timeAt(e->position().x());
    m_sel = {qMin(m_dragFrom, t), qMax(m_dragFrom, t)};
    update();
    emit selectionChanged(m_sel);
}

void WaveformView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragFrom < 0)
        return;
    const double t = timeAt(e->position().x());
    if (std::abs(xAt(t) - xAt(m_dragFrom)) < 3) {   // simple clic : position de lecture
        m_sel = {-1, -1};
        emit selectionChanged(m_sel);
        emit cursorClicked(t);
    } else {
        m_sel = {qMin(m_dragFrom, t), qMax(m_dragFrom, t)};
        emit selectionChanged(m_sel);
    }
    m_dragFrom = -1;
    update();
}

void WaveformView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (e->modifiers() & Qt::ControlModifier)
        zoom(std::pow(1.25, steps), timeAt(e->position().x()));
    else
        setView(m_viewStart - steps * m_viewLen * 0.1, m_viewLen);
    e->accept();
}

// ---------------------------------------------------------------- fenêtre

TrimDialog::TrimDialog(const QString &audioFile, const QString &title, QWidget *parent) : QDialog(parent), m_file(audioFile)
{
    setWindowTitle(tr("Découper — %1").arg(title));
    resize(980, 420);
    auto *lay = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Glissez sur la forme d'onde pour sélectionner un passage, puis « Supprimer la sélection ». "
                               "Ctrl + molette : zoom ; molette : défilement ; clic : position de lecture. "
                               "La lecture saute les passages retirés."),
                            this);
    help->setWordWrap(true);
    help->setStyleSheet("color: palette(placeholder-text);");
    lay->addWidget(help);
    m_wave = new WaveformView(this);
    lay->addWidget(m_wave, 1);
    m_scroll = new QScrollBar(Qt::Horizontal, this);
    lay->addWidget(m_scroll);

    auto *row = new QHBoxLayout;
    m_transport = new AudioTransport(this);
    m_playSel = new QPushButton(QIcon::fromTheme("media-playback-start"), tr("Écouter la sélection"), this);
    auto *zin = new QPushButton(QIcon::fromTheme("zoom-in"), QString(), this);
    auto *zout = new QPushButton(QIcon::fromTheme("zoom-out"), QString(), this);
    auto *zall = new QPushButton(QIcon::fromTheme("zoom-fit-best"), QString(), this);
    zin->setToolTip(tr("Zoom avant"));
    zout->setToolTip(tr("Zoom arrière"));
    zall->setToolTip(tr("Tout voir"));
    row->addWidget(m_transport, 1);
    row->addWidget(m_playSel);
    row->addWidget(zin);
    row->addWidget(zout);
    row->addWidget(zall);
    lay->addLayout(row);

    auto *edit = new QHBoxLayout;
    m_remove = new QPushButton(QIcon::fromTheme("edit-cut"), tr("Supprimer la sélection"), this);
    m_remove->setShortcut(QKeySequence::Delete);
    m_keep = new QPushButton(QIcon::fromTheme("transform-crop"), tr("Garder seulement la sélection"), this);
    m_reset = new QPushButton(QIcon::fromTheme("edit-undo"), tr("Tout rétablir"), this);
    m_selInfo = new QLabel(this);
    edit->addWidget(m_remove);
    edit->addWidget(m_keep);
    edit->addWidget(m_reset);
    edit->addStretch();
    edit->addWidget(m_selInfo);
    lay->addLayout(edit);

    auto *bottom = new QHBoxLayout;
    m_info = new QLabel(this);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Appliquer"));
    bottom->addWidget(m_info, 1);
    bottom->addWidget(bb);
    lay->addLayout(bottom);

    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(new QAudioOutput(this));
    m_player->setSource(QUrl::fromLocalFile(m_file));
    m_transport->setPlayer(m_player);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_wave, &WaveformView::selectionChanged, this, &TrimDialog::refresh);
    connect(m_wave, &WaveformView::cursorClicked, this, [this](double t) { m_player->setPosition(qint64(t * 1000)); });
    connect(m_wave, &WaveformView::viewChanged, this, [this]() {
        const double d = m_wave->duration();
        m_scroll->blockSignals(true);
        m_scroll->setRange(0, int(std::max(0.0, d - m_wave->viewLength()) * 10));
        m_scroll->setPageStep(int(m_wave->viewLength() * 10));
        m_scroll->setValue(int(m_wave->viewStart() * 10));
        m_scroll->blockSignals(false);
    });
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int v) { m_wave->setView(v / 10.0, m_wave->viewLength()); });
    connect(zin, &QPushButton::clicked, this, [this]() { m_wave->zoom(2, m_wave->viewStart() + m_wave->viewLength() / 2); });
    connect(zout, &QPushButton::clicked, this, [this]() { m_wave->zoom(0.5, m_wave->viewStart() + m_wave->viewLength() / 2); });
    connect(zall, &QPushButton::clicked, this, [this]() { m_wave->setView(0, m_wave->duration()); });
    connect(m_playSel, &QPushButton::clicked, this, [this]() {
        const Span s = m_wave->selection();
        if (s.second > s.first)
            playFrom(s.first, s.second);
    });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        removeSpan(m_wave->selection());
        m_wave->setSelection({-1, -1});
    });
    connect(m_keep, &QPushButton::clicked, this, [this]() {
        const Span s = m_wave->selection();
        if (s.second <= s.first)
            return;
        removeSpan({0, s.first});
        removeSpan({s.second, m_wave->duration()});
        m_wave->setSelection({-1, -1});
    });
    connect(m_reset, &QPushButton::clicked, this, [this]() {
        m_removed.clear();
        m_wave->setRemoved(m_removed);
        refresh();
    });
    // lecture : saute les passages retirés, s'arrête en fin de sélection écoutée
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        const double t = ms / 1000.0;
        m_wave->setPlayhead(t);
        if (m_stopAt >= 0 && t >= m_stopAt) {
            m_player->pause();
            m_stopAt = -1;
            return;
        }
        for (const Span &r : std::as_const(m_removed))
            if (t >= r.first && t < r.second - 0.05 && m_stopAt < 0) {
                m_player->setPosition(qint64(r.second * 1000));
                return;
            }
    });
    load();
    refresh();
}

void TrimDialog::load()
{
    // enveloppe : décodage mono 8 kHz, maximum absolu par centième de seconde
    auto *w = new QFutureWatcher<QVector<float>>(this);
    connect(w, &QFutureWatcher<QVector<float>>::finished, this, [this, w]() {
        QVector<float> env = w->result();
        w->deleteLater();
        float peak = 0.0001f;
        for (float v : std::as_const(env))
            peak = qMax(peak, v);
        for (float &v : env)
            v = std::sqrt(v / peak);   // racine : rend visibles les passages calmes
        m_wave->setEnvelope(env, ENV_RATE);
        refresh();
    });
    const QString file = m_file;
    w->setFuture(QtConcurrent::run([file]() {
        QVector<float> env;
        QProcess p;
        p.start(lunii::ffmpegPath(), {"-v", "error", "-i", file, "-map", "0:a:0", "-ac", "1", "-ar", "8000", "-f", "s16le", "-"});
        if (!p.waitForStarted())
            return env;
        const int per = int(8000 / ENV_RATE);
        QByteArray buf;
        int count = 0;
        float cur = 0;
        auto consume = [&]() {
            const int n = int(buf.size() / 2);
            const qint16 *s = reinterpret_cast<const qint16 *>(buf.constData());
            for (int i = 0; i < n; ++i) {
                cur = qMax(cur, float(std::abs(int(s[i]))) / 32768.f);
                if (++count == per) {
                    env << cur;
                    cur = 0;
                    count = 0;
                }
            }
            buf.remove(0, n * 2);
        };
        while (p.waitForReadyRead(-1)) {
            buf += p.readAllStandardOutput();
            consume();
        }
        p.waitForFinished();
        buf += p.readAllStandardOutput();
        consume();
        if (count)
            env << cur;
        return env;
    }));
}

void TrimDialog::removeSpan(const Span &s)
{
    const double d = m_wave->duration();
    Span n{qBound(0.0, s.first, d), qBound(0.0, s.second, d)};
    if (n.second - n.first < 0.05)
        return;
    // fusion avec les passages qui se chevauchent
    QList<Span> out;
    for (const Span &r : std::as_const(m_removed)) {
        if (r.second < n.first || r.first > n.second)
            out << r;
        else
            n = {qMin(n.first, r.first), qMax(n.second, r.second)};
    }
    out << n;
    std::sort(out.begin(), out.end());
    m_removed = out;
    m_wave->setRemoved(m_removed);
    refresh();
}

QList<Span> TrimDialog::kept() const
{
    QList<Span> out;
    double t = 0;
    for (const Span &r : m_removed) {
        if (r.first - t > 0.05)
            out << Span{t, r.first};
        t = r.second;
    }
    if (m_wave->duration() - t > 0.05)
        out << Span{t, m_wave->duration()};
    return out;
}

void TrimDialog::refresh()
{
    const Span s = m_wave->selection();
    const bool sel = s.second > s.first;
    m_remove->setEnabled(sel);
    m_keep->setEnabled(sel);
    m_playSel->setEnabled(sel);
    m_reset->setEnabled(!m_removed.isEmpty());
    m_selInfo->setText(sel ? tr("Sélection : %1 → %2 (%3 s)").arg(stamp(s.first), stamp(s.second)).arg(s.second - s.first, 0, 'f', 1)
                           : QString());
    double left = 0;
    for (const Span &k : kept())
        left += k.second - k.first;
    m_info->setText(m_wave->duration() <= 0 ? QString()
                    : tr("Durée : %1 → %2 (%n passage(s) retiré(s))", nullptr, int(m_removed.size()))
                          .arg(stamp(m_wave->duration()), stamp(left)));
}

void TrimDialog::playFrom(double t, double until)
{
    m_stopAt = until;
    m_player->setPosition(qint64(t * 1000));
    m_player->play();
}

QByteArray TrimDialog::render(const QString &audioFile, const QList<Span> &kept, QString *error)
{
    if (kept.isEmpty()) {
        if (error)
            *error = QObject::tr("Rien à garder");
        return {};
    }
    // un morceau par passage conservé, fondus très courts aux coupes pour éviter les clics
    QString graph;
    for (int k = 0; k < kept.size(); ++k) {
        const double len = kept[k].second - kept[k].first;
        graph += QString("[0:a:0]atrim=start=%1:end=%2,asetpts=PTS-STARTPTS,aformat=channel_layouts=mono,"
                         "afade=t=in:d=0.015,afade=t=out:st=%3:d=0.015[p%4];")
                     .arg(kept[k].first, 0, 'f', 3).arg(kept[k].second, 0, 'f', 3).arg(qMax(0.0, len - 0.015), 0, 'f', 3).arg(k);
    }
    for (int k = 0; k < kept.size(); ++k)
        graph += QString("[p%1]").arg(k);
    graph += QString("concat=n=%1:v=0:a=1[out]").arg(kept.size());
    QProcess p;
    p.start(lunii::ffmpegPath(), {"-v", "error", "-i", audioFile, "-filter_complex", graph, "-map", "[out]", "-ac", "1",
                                  "-ar", "44100", "-b:a", "128k", "-map_metadata", "-1", "-id3v2_version", "0",
                                  "-write_xing", "0", "-f", "mp3", "pipe:1"});
    if (!p.waitForStarted()) {
        if (error)
            *error = QObject::tr("ffmpeg introuvable");
        return {};
    }
    p.waitForFinished(-1);
    const QByteArray out = p.readAllStandardOutput();
    if (p.exitCode() != 0 || out.isEmpty()) {
        if (error)
            *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        return {};
    }
    return out;
}
