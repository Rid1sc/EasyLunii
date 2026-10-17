// Découpe d'un son : retirer des passages (générique, publicité…) en s'aidant de la forme d'onde.
#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QVector>

class AudioTransport;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QScrollBar;

using Span = QPair<double, double>;   // début, fin (secondes)

class WaveformView : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformView(QWidget *parent = nullptr);
    void setEnvelope(const QVector<float> &env, double perSecond);
    double duration() const { return m_perSecond > 0 ? m_env.size() / m_perSecond : 0; }

    Span selection() const { return m_sel; }
    void setSelection(const Span &s);
    void setRemoved(const QList<Span> &r);
    void setPlayhead(double t);
    void setView(double start, double length);   // portion visible
    double viewStart() const { return m_viewStart; }
    double viewLength() const { return m_viewLen; }
    void zoom(double factor, double around);

signals:
    void selectionChanged(Span s);
    void cursorClicked(double t);
    void viewChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    double timeAt(double x) const;
    double xAt(double t) const;

    QVector<float> m_env;
    double m_perSecond = 0;
    double m_viewStart = 0, m_viewLen = 0;
    Span m_sel{-1, -1};
    QList<Span> m_removed;
    double m_playhead = -1;
    double m_dragFrom = -1;
};

class TrimDialog : public QDialog
{
    Q_OBJECT
    friend struct EditorPageTest;
public:
    TrimDialog(const QString &audioFile, const QString &title, QWidget *parent = nullptr);
    // passages conservés (début, fin) en secondes
    QList<Span> kept() const;
    bool changed() const { return !m_removed.isEmpty(); }
    // Applique la découpe : MP3 mono 44,1 kHz, fondus de 15 ms aux points de coupe ; vide si erreur
    static QByteArray render(const QString &audioFile, const QList<Span> &kept, QString *error = nullptr);

    void removeSpan(const Span &s);   // ajoute un passage retiré (fusionné avec les voisins)

private:
    void load();
    void refresh();
    void playFrom(double t, double until = -1);

    QString m_file;
    WaveformView *m_wave;
    QScrollBar *m_scroll;
    QLabel *m_info, *m_selInfo;
    QPushButton *m_remove, *m_keep, *m_reset, *m_playSel;
    QMediaPlayer *m_player;
    AudioTransport *m_transport;
    QList<Span> m_removed;
    double m_stopAt = -1;
};
