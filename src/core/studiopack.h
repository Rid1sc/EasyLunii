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

    // Ressources : en mémoire (images) ou sur disque (sons, gros fichiers) pour limiter la RAM
    QHash<QString, QByteArray> assets;
    QHash<QString, QString> assetFiles;   // nom -> fichier local
    QByteArray thumbnail;

    bool hasAsset(const QString &name) const { return assets.contains(name) || assetFiles.contains(name); }
    QByteArray assetData(const QString &name) const;
    qint64 assetSize(const QString &name) const;
    // Range une ressource : sur disque dans spillDir si c'est un son ou si elle est volumineuse
    void putAsset(const QString &name, const QByteArray &data, const QString &spillDir);

    static StudioPack fromJson(const QJsonObject &root);
    QJsonObject toJson() const;

    // Archive STUdio (zip/7z) : story.json + assets/ + thumbnail.png
    // spillDir non vide : les sons sont écrits dans ce dossier au lieu de rester en mémoire
    static bool loadArchive(const QString &path, StudioPack &out, QString *error = nullptr, bool withAssets = true,
                            const QString &spillDir = {});
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
