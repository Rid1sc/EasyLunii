// Barre de lecture de test : reculer/avancer de 10 s, lecture/pause, position. Indépendante des
// commandes de la Lunii (sert à vérifier un son, pas à simuler l'appareil).
#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QMediaPlayer;
class QSlider;
class QToolButton;

class AudioTransport : public QWidget
{
    Q_OBJECT
public:
    explicit AudioTransport(QWidget *parent = nullptr);
    void setPlayer(QMediaPlayer *player);

    void seekBy(qint64 ms);
    void togglePlay();

private:
    void sync();

    QPointer<QMediaPlayer> m_player;
    QToolButton *m_back, *m_play, *m_fwd;
    QSlider *m_slider;
    QLabel *m_time;
};
