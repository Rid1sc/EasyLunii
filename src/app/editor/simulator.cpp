#include "app/editor/simulator.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>

using lunii::Transition;

Simulator::Simulator(const lunii::StudioPack &pack, QWidget *parent) : QDialog(parent), m_pack(pack)
{
    setWindowTitle(tr("Simulateur — %1").arg(pack.title));
    auto *lay = new QVBoxLayout(this);
    m_screen = new QLabel(this);
    m_screen->setFixedSize(640, 480);
    m_screen->setAlignment(Qt::AlignCenter);
    m_screen->setStyleSheet("background: black; color: #aaa; border-radius: 12px;");
    lay->addWidget(m_screen, 0, Qt::AlignHCenter);
    m_status = new QLabel(this);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    auto *row = new QHBoxLayout;
    m_home = new QPushButton(QString::fromUtf8("⌂  Maison"), this);
    m_left = new QPushButton(QString::fromUtf8("◀"), this);
    m_ok = new QPushButton(tr("OK"), this);
    m_right = new QPushButton(QString::fromUtf8("▶"), this);
    m_pause = new QPushButton(QString::fromUtf8("❚❚  Pause"), this);
    for (auto *b : {m_home, m_left, m_ok, m_right, m_pause}) {
        b->setMinimumHeight(40);
        b->setFocusPolicy(Qt::NoFocus);
        row->addWidget(b);
    }
    lay->addLayout(row);
    auto *hint = new QLabel(tr("Clavier : ← → molette, Entrée OK, Échap ou Retour arrière maison, Espace pause"), this);
    hint->setStyleSheet("color: palette(placeholder-text);");
    hint->setAlignment(Qt::AlignCenter);
    lay->addWidget(hint);

    m_player = new QMediaPlayer(this);
    m_out = new QAudioOutput(this);
    m_player->setAudioOutput(m_out);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia && m_stage >= 0 && m_pack.stages[m_stage].ctrl.autoplay)
            QTimer::singleShot(0, this, [this]() { follow(m_pack.stages[m_stage].ok); });
    });
    connect(m_left, &QPushButton::clicked, this, [this]() { wheel(-1); });
    connect(m_right, &QPushButton::clicked, this, [this]() { wheel(1); });
    connect(m_ok, &QPushButton::clicked, this, &Simulator::ok);
    connect(m_home, &QPushButton::clicked, this, &Simulator::home);
    connect(m_pause, &QPushButton::clicked, this, &Simulator::pause);

    if (!pack.stages.isEmpty())
        enter(0, {}, 0);
}

Simulator::~Simulator()
{
    m_player->stop();
}

void Simulator::enter(int stage, const QString &actionId, int optionIndex)
{
    m_player->stop();
    m_player->setSourceDevice(nullptr);
    delete m_buf;
    m_buf = nullptr;
    m_stage = stage;
    m_action = actionId;
    m_option = optionIndex;
    const lunii::StageNode &n = m_pack.stages[stage];

    QImage img;
    if (!n.image.isEmpty())
        img.loadFromData(m_pack.assetData(n.image));
    if (img.isNull())
        m_screen->setText(n.image.isEmpty() ? tr("(écran noir)") : tr("(image absente)"));
    else
        m_screen->setPixmap(QPixmap::fromImage(img.scaled(640, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation)));

    QString where = n.name.isEmpty() ? tr("Scène") : n.name;
    if (!m_action.isEmpty()) {
        const int ai = m_pack.actionIndex(m_action);
        if (ai >= 0 && m_pack.actions[ai].options.size() > 1)
            where += tr("  —  option %1/%2").arg(m_option + 1).arg(m_pack.actions[ai].options.size());
    }
    m_status->setText(where);

    if (!n.audio.isEmpty() && m_pack.hasAsset(n.audio)) {
        if (m_pack.assetFiles.contains(n.audio)) {
            m_player->setSource(QUrl::fromLocalFile(m_pack.assetFiles.value(n.audio)));
        } else {
            m_buf = new QBuffer(this);
            m_buf->setData(m_pack.assets.value(n.audio));
            m_buf->open(QIODevice::ReadOnly);
            m_player->setSourceDevice(m_buf, QUrl(n.audio));
        }
        m_player->play();
    } else if (n.ctrl.autoplay) {
        // pas de son : la Lunii enchaîne immédiatement
        QTimer::singleShot(300, this, [this, stage]() {
            if (m_stage == stage)
                follow(m_pack.stages[stage].ok);
        });
    }
    updateButtons();
}

bool Simulator::follow(const Transition &t)
{
    const int ai = m_pack.actionIndex(t.actionId);
    if (!t.isSet() || ai < 0 || m_pack.actions[ai].options.isEmpty()) {
        m_status->setText(m_status->text() + tr("\nFin : pas de transition, la Lunii s'arrêterait ici."));
        return false;
    }
    const auto &opts = m_pack.actions[ai].options;
    int idx = t.optionIndex < 0 ? int(QRandomGenerator::global()->bounded(opts.size())) : qMin(t.optionIndex, int(opts.size()) - 1);
    const int si = m_pack.stageIndex(opts[idx]);
    if (si < 0)
        return false;
    enter(si, t.actionId, idx);
    return true;
}

void Simulator::wheel(int delta)
{
    if (m_stage < 0 || !m_pack.stages[m_stage].ctrl.wheel || m_action.isEmpty())
        return;
    const int ai = m_pack.actionIndex(m_action);
    if (ai < 0)
        return;
    const auto &opts = m_pack.actions[ai].options;
    const int n = int(opts.size());
    const int idx = ((m_option + delta) % n + n) % n;
    const int si = m_pack.stageIndex(opts[idx]);
    if (si >= 0)
        enter(si, m_action, idx);
}

void Simulator::ok()
{
    if (m_stage >= 0 && m_pack.stages[m_stage].ctrl.ok)
        follow(m_pack.stages[m_stage].ok);
}

void Simulator::home()
{
    if (m_stage < 0 || !m_pack.stages[m_stage].ctrl.home)
        return;
    if (!follow(m_pack.stages[m_stage].home))
        enter(0, {}, 0);   // sans transition maison, retour à la couverture
}

void Simulator::pause()
{
    if (m_stage < 0 || !m_pack.stages[m_stage].ctrl.pause)
        return;
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else if (m_player->playbackState() == QMediaPlayer::PausedState)
        m_player->play();
}

void Simulator::updateButtons()
{
    const auto &c = m_pack.stages[m_stage].ctrl;
    m_left->setEnabled(c.wheel && !m_action.isEmpty());
    m_right->setEnabled(c.wheel && !m_action.isEmpty());
    m_ok->setEnabled(c.ok);
    m_home->setEnabled(c.home);
    m_pause->setEnabled(c.pause);
}

void Simulator::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Left: wheel(-1); break;
    case Qt::Key_Right: wheel(1); break;
    case Qt::Key_Return:
    case Qt::Key_Enter: ok(); break;
    case Qt::Key_Backspace: home(); break;
    case Qt::Key_Space: pause(); break;
    case Qt::Key_Escape:
        if (m_pack.stages[m_stage].ctrl.home && m_stage != 0)
            home();
        else
            QDialog::keyPressEvent(e);
        break;
    default: QDialog::keyPressEvent(e);
    }
}
