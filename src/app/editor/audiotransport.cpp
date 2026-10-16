#include "app/editor/audiotransport.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMediaPlayer>
#include <QSlider>
#include <QToolButton>

static QString clock(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

AudioTransport::AudioTransport(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto button = [this, lay](const char *icon, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setIcon(QIcon::fromTheme(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        lay->addWidget(b);
        return b;
    };
    m_back = button("media-seek-backward", tr("Reculer de 10 s"));
    m_play = button("media-playback-start", tr("Lecture / pause"));
    m_fwd = button("media-seek-forward", tr("Avancer de 10 s"));
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setFocusPolicy(Qt::NoFocus);
    lay->addWidget(m_slider, 1);
    m_time = new QLabel(this);
    m_time->setMinimumWidth(m_time->fontMetrics().horizontalAdvance(QStringLiteral("00:00 / 00:00")));
    lay->addWidget(m_time);

    connect(m_back, &QToolButton::clicked, this, [this]() { seekBy(-10000); });
    connect(m_fwd, &QToolButton::clicked, this, [this]() { seekBy(10000); });
    connect(m_play, &QToolButton::clicked, this, &AudioTransport::togglePlay);
    // pendant le glisser, on n'envoie la position qu'au relâchement (le décodage suit mal sinon)
    connect(m_slider, &QSlider::sliderMoved, this, [this](int v) {
        if (m_player)
            m_time->setText(clock(v) + " / " + clock(m_player->duration()));
    });
    connect(m_slider, &QSlider::sliderReleased, this, [this]() {
        if (m_player)
            m_player->setPosition(m_slider->value());
    });
    connect(m_slider, &QSlider::actionTriggered, this, [this](int action) {
        // clic sur la piste : saut direct
        if (m_player && action != QAbstractSlider::SliderMove)
            m_player->setPosition(m_slider->sliderPosition());
    });
    sync();
}

void AudioTransport::setPlayer(QMediaPlayer *player)
{
    if (m_player)
        m_player->disconnect(this);
    m_player = player;
    if (player) {
        connect(player, &QMediaPlayer::durationChanged, this, &AudioTransport::sync);
        connect(player, &QMediaPlayer::playbackStateChanged, this, &AudioTransport::sync);
        connect(player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            if (!m_slider->isSliderDown())
                m_slider->setValue(int(pos));
            m_time->setText(clock(pos) + " / " + clock(m_player->duration()));
        });
    }
    sync();
}

void AudioTransport::seekBy(qint64 ms)
{
    if (m_player && m_player->duration() > 0)
        m_player->setPosition(qBound<qint64>(0, m_player->position() + ms, m_player->duration()));
}

void AudioTransport::togglePlay()
{
    if (!m_player || m_player->duration() <= 0)
        return;
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void AudioTransport::sync()
{
    const qint64 d = m_player ? m_player->duration() : 0;
    const bool has = d > 0;
    for (QWidget *w : {static_cast<QWidget *>(m_back), static_cast<QWidget *>(m_play),
                       static_cast<QWidget *>(m_fwd), static_cast<QWidget *>(m_slider)})
        w->setEnabled(has);
    m_slider->setRange(0, int(d));
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    m_play->setIcon(QIcon::fromTheme(playing ? "media-playback-pause" : "media-playback-start"));
    m_time->setText(has ? clock(m_player->position()) + " / " + clock(d) : QStringLiteral("–:– / –:–"));
}
