// Simulateur : parcourt un pack comme le ferait la Lunii (molette, OK, maison, pause, lecture auto).
#pragma once

#include "core/studiopack.h"

#include <QDialog>

class QAudioOutput;
class QBuffer;
class QLabel;
class QMediaPlayer;
class QPushButton;

class Simulator : public QDialog
{
    Q_OBJECT
public:
    Simulator(const lunii::StudioPack &pack, QWidget *parent = nullptr);
    ~Simulator() override;

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void enter(int stage, const QString &actionId, int optionIndex);
    bool follow(const lunii::Transition &t);
    void wheel(int delta);
    void ok();
    void home();
    void pause();
    void updateButtons();

    const lunii::StudioPack &m_pack;
    int m_stage = -1;
    QString m_action;     // choix dans lequel la molette fait défiler
    int m_option = 0;
    QLabel *m_screen, *m_status;
    QPushButton *m_left, *m_right, *m_ok, *m_home, *m_pause;
    QMediaPlayer *m_player;
    QAudioOutput *m_out;
    QBuffer *m_buf = nullptr;
};
