// « Depuis une source… » : parcourir un plugin (recherche ou lien), choisir des sons, les télécharger.
#pragma once

#include "app/sources/plugin.h"

#include <QDialog>
#include <QPointer>

class AudioTransport;
class JobQueue;
class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QMediaPlayer;
class QNetworkAccessManager;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class SourceDialog : public QDialog
{
    Q_OBJECT
    friend struct EditorPageTest;   // tests d'interface
public:
    enum Mode { Stories, SingleSound };   // assistant (plusieurs histoires) ou éditeur (un son)

    // Histoire prête pour l'assistant : fichiers locaux, dans l'ordre
    struct Story {
        QString title, image;
        QStringList parts;
    };

    SourceDialog(Mode mode, JobQueue *jobs, QWidget *parent = nullptr);
    QList<Story> stories() const { return m_result; }
    QString soundFile() const { return m_result.isEmpty() || m_result.first().parts.isEmpty() ? QString() : m_result.first().parts.first(); }
    bool hasPlugins() const { return !m_plugins.isEmpty(); }

    void accept() override;

private:
    void pluginChanged();
    void go();                                     // recherche ou ouverture du lien saisi
    void openCollection(const SourceItem &item);
    void showItems(const QList<SourceItem> &items, const QString &title);
    void runQuery(const std::function<QList<SourceItem>(PluginRunner &, QString *)> &query, const QString &busy);
    void loadThumb(QTreeWidgetItem *it, const QString &url);
    void selectionChanged();
    void updateCount();
    QList<SourceItem> checkedItems() const;

    Mode m_mode;
    QPointer<JobQueue> m_jobs;
    QList<PluginInfo> m_plugins;
    QComboBox *m_plugin;
    QLabel *m_about, *m_where, *m_details, *m_status;
    QLineEdit *m_query;
    QPushButton *m_go, *m_back, *m_add, *m_checkAll, *m_preview;
    QTreeWidget *m_list;
    QButtonGroup *m_grouping = nullptr;
    QLineEdit *m_groupTitle = nullptr;
    QMediaPlayer *m_player = nullptr;
    AudioTransport *m_transport;
    QNetworkAccessManager *m_net;
    QList<SourceItem> m_items;                         // éléments affichés
    QList<QPair<QString, QList<SourceItem>>> m_history; // pour « Retour »
    QString m_collectionTitle, m_collectionImage;
    int m_generation = 0;                              // ignore les réponses dépassées
    QList<Story> m_result;
};
