// Onglet Éditeur : graphe de nœuds façon STUdio, inspecteur, simulateur, envoi sur la Lunii.
#pragma once

#include "core/studiopack.h"

#include <QGraphicsView>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QWidget>

class JobQueue;
class NodeItem;
class PortItem;
class QAudioOutput;
class QBuffer;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class AudioTransport;
class QMediaPlayer;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolBar;

class GraphView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit GraphView(QWidget *parent = nullptr);
    void zoomToFit();
    void showStart();

signals:
    void linkRequested(const QString &fromId, int portRole, int portIndex, const QString &toId);
    void addRequested(bool stage, const QPointF &scenePos);
    void deleteRequested();
    void filesDropped(const QStringList &files, const QPointF &scenePos);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void drawBackground(QPainter *p, const QRectF &rect) override;

private:
    PortItem *m_dragPort = nullptr;
    QGraphicsPathItem *m_temp = nullptr;
    bool m_panning = false;
    QPoint m_panStart;
};

class EditorPage : public QWidget
{
    Q_OBJECT
    friend struct EditorPageTest;   // tests d'interface
public:
    EditorPage(JobQueue *jobs, QWidget *parent = nullptr);

    bool maybeSave();
    void newPack();
    void openDialog();
    bool openPack(const QString &path);

    // utilisés par la commande d'annulation
    QJsonObject snapshot() const { return m_pack.toJson(); }
    void restore(const QJsonObject &json);

signals:
    void logMessage(int level, const QString &message);
    void sendToDevice(const QString &packPath);

private:
    void buildUi();
    void rebuildScene();
    void change(const QString &label, const std::function<void()> &mutate);
    bool save();
    bool saveAs();
    void updateTitle();
    void selectionChanged();
    void showPackPanel();
    void showStagePanel(int index);
    void showActionPanel(int index);
    void link(const QString &fromId, int portRole, int portIndex, const QString &toId);
    void addNode(bool stage, const QPointF &pos);
    void deleteSelection();
    void autoLayout();
    void validate();
    void simulate();
    void send();
    void wizard();
    void importFiles(const QStringList &files, const QPointF &pos);
    QString addAsset(const QByteArray &data, const QString &ext);
    QString importImage(const QString &file);
    QString importAudio(const QString &file);
    QPixmap assetPixmap(const QString &asset, const QSize &size) const;
    QString selectedId() const;
    void playAsset(const QString &asset);
    void stopAudio();
    QMediaPlayer *player();

    JobQueue *m_jobs;
    lunii::StudioPack m_pack;
    QString m_path;
    QUndoStack m_undo;
    QTemporaryDir m_spill;   // sons du pack ouvert (hors mémoire)
    bool m_rebuilding = false;
    bool m_showHome = true;
    QString m_selected;    // id du nœud affiché dans l'inspecteur

    QToolBar *m_bar;
    GraphView *m_view;
    QGraphicsScene *m_scene;
    QStackedWidget *m_stack;
    QLabel *m_fileLabel;

    // panneau pack
    QLineEdit *m_packTitle;
    QPlainTextEdit *m_packDesc;
    QSpinBox *m_packVersion;
    QCheckBox *m_packNight;
    QLabel *m_packThumb, *m_packStats;
    QListWidget *m_issues;

    // panneau scène
    QLineEdit *m_stageName;
    QLabel *m_stageStart, *m_stageImage, *m_stageAudio;
    QPushButton *m_setStart;
    QCheckBox *m_ctrl[5];
    QComboBox *m_okAction, *m_homeAction;
    QSpinBox *m_okIndex, *m_homeIndex;

    // panneau choix
    QLineEdit *m_actionName;
    QListWidget *m_options;
    QComboBox *m_addOption;

    QMediaPlayer *m_player;
    QAudioOutput *m_audioOut;
    QBuffer *m_audioBuf = nullptr;
    AudioTransport *m_transport;
};
