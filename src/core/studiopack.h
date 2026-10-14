// Modèle d'un pack au format STUdio (story.json v1 + assets/), utilisé par l'import et par l'éditeur.
// Lecture/écriture : format de studio/core/v1 (ArchiveStoryPackReader/Writer, MPL-2.0).
// Génération des index Lunii (ri/si/li/ni) : port de StudioStory (pkg/api/stories.py, Lunii.QT).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace lunii {

struct Transition {
    QString actionId;     // vide = pas de transition
    int optionIndex = 0;  // -1 = choix aléatoire côté Lunii
    bool isSet() const { return !actionId.isEmpty(); }
};

struct ControlSettings {
    bool wheel = false, ok = false, home = false, pause = false, autoplay = false;
};

struct StageNode {
    QString uuid;
    QString name;
    QString type;          // métadonnées STUdio facultatives (cover, menu.questionstage…)
    QString groupId;
    QPointF pos;
    QString image;         // nom d'asset, vide si aucun
    QString audio;
    Transition ok, home;
    ControlSettings ctrl;
    QJsonObject extra;     // champs inconnus conservés tels quels
};

struct ActionNode {
    QString id;
    QString name;
    QString type;
    QString groupId;
    QPointF pos;
    QStringList options;   // UUID des stage nodes
    QJsonObject extra;
};

class StudioPack
{
public:
    QString format = "v1";
    int version = 1;
    QString title;
    QString description;
    bool nightMode = false;
    QList<StageNode> stages;      // stages[0] = nœud de départ (squareOne)
    QList<ActionNode> actions;
    QJsonObject extra;

    // Assets en mémoire (nom -> contenu) ; pour l'éditeur, chargés depuis l'archive
    QHash<QString, QByteArray> assets;
    QByteArray thumbnail;

    static StudioPack fromJson(const QJsonObject &root);
    QJsonObject toJson() const;

    // Archive STUdio (zip/7z) : story.json + assets/ + thumbnail.png
    static bool loadArchive(const QString &path, StudioPack &out, QString *error = nullptr, bool withAssets = true);
    bool saveArchive(const QString &path, QString *error = nullptr) const;

    QUuid packUuid() const;       // UUID du premier stage node = UUID du pack
    int stageIndex(const QString &uuid) const;
    int actionIndex(const QString &id) const;

    // --- Conversion vers les index Lunii
    struct LuniiIndex {
        QStringList riOrder, siOrder;                 // assets dans l'ordre d'apparition
        QHash<QString, QString> riName, siName;       // asset -> nom 8 caractères sur la Lunii
        QHash<QString, int> riPos, siPos;
        QList<int> li;
        QHash<QString, int> actionGlobalIndex;
    };
    LuniiIndex luniiIndex() const;
    static QByteArray riData(const LuniiIndex &ix);
    static QByteArray siData(const LuniiIndex &ix);
    static QByteArray liData(const LuniiIndex &ix);
    QByteArray niData(const LuniiIndex &ix) const;

    // Contrôles de cohérence (transitions vers des nœuds absents, assets manquants…)
    QStringList validate() const;
};

} // namespace lunii
