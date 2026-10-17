// Assistant « nouveau pack » : des fichiers audio (ou des zips/dossiers de parties) vers un pack à menus.
#pragma once

#include "core/studiopack.h"

#include <QDialog>

class JobQueue;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

class PackWizard : public QDialog
{
    Q_OBJECT
    friend struct EditorPageTest;   // tests d'interface
public:
    explicit PackWizard(const QString &spillDir, QWidget *parent = nullptr);
    lunii::StudioPack result() const { return m_result; }
    void setJobQueue(JobQueue *jobs) { m_jobs = jobs; }   // téléchargements depuis les sources

    struct StoryInput {
        QString title;
        QStringList parts;   // fichiers audio, dans l'ordre
        QString image;       // image choisie (sinon jaquette intégrée ou carte de titre)
        QString announce;    // enregistrement du titre (sinon « titre.* » du dossier/zip, puis Piper)
    };

private:
    void addFiles();
    void addAudioFiles(QStringList files);
    void addContainers();
    void removeSelected();
    void moveSelected(int d);
    void accept() override;
    void refresh();

    QList<StoryInput> m_stories;
    QLineEdit *m_title;
    QTableWidget *m_table;
    QSpinBox *m_groupSize;
    QCheckBox *m_tts;
    QComboBox *m_voice;
    QSpinBox *m_speaker;
    QString m_coverAnnounce;   // enregistrement de l'accueil du pack
    QLabel *m_coverLabel;
    lunii::StudioPack m_result;
    QString m_spillDir;
    JobQueue *m_jobs = nullptr;
};
