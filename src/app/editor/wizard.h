// Assistant « nouveau pack » : des fichiers audio (ou des zips/dossiers de parties) vers un pack à menus.
#pragma once

#include "core/studiopack.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

class PackWizard : public QDialog
{
    Q_OBJECT
public:
    explicit PackWizard(const QString &spillDir, QWidget *parent = nullptr);
    lunii::StudioPack result() const { return m_result; }

    struct StoryInput {
        QString title;
        QStringList parts;   // fichiers audio, dans l'ordre
        QString image;       // image choisie (sinon jaquette intégrée ou carte de titre)
    };

private:
    void addFiles();
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
    lunii::StudioPack m_result;
    QString m_spillDir;
};
