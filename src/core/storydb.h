// Bases de titres des histoires (officielle Lunii + tierces), partagées avec Lunii.QT dans ~/.lunii-qt.
// Port de pkg/api/stories.py (story_load_db, thirdparty_db_*, Story.name/desc/author/picture).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QUuid>

namespace lunii {

QString configDir();   // ~/.lunii-qt
QString cacheDir();    // ~/.lunii-qt/cache

// Représentation canonique : "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" en majuscules
QString uuidStr(const QUuid &u);
QString shortUuid(const QUuid &u); // 8 derniers caractères hexadécimaux, majuscules

class StoryDb
{
public:
    static StoryDb &instance();

    // Charge les bases ; télécharge la base officielle si absente (ou si refreshOfficial).
    // Appel bloquant (réseau) : à lancer hors du thread de l'interface.
    bool load(bool refreshOfficial = false);

    bool isOfficial(const QUuid &u) const;
    bool isKnown(const QUuid &u) const;
    QString title(const QUuid &u) const;       // chaîne vide si inconnue
    QString description(const QUuid &u) const;
    QString author(const QUuid &u) const;
    QString pictureUrl(const QUuid &u) const;
    QString ageLabel(const QUuid &u) const;    // ex. "3+" pour les histoires officielles, sinon vide

    // Jaquette depuis le cache ; si absente et download=true, téléchargement (bloquant)
    QByteArray picture(const QUuid &u, bool download = false) const;

    void addThirdParty(const QUuid &u, const QString &title, const QString &desc);
    void addThumb(const QUuid &u, const QByteArray &data);
    QByteArray thirdPartyMeta(const QUuid &u) const; // JSON de la fiche tierce (export .plain.pk)

    // UUID complet à partir d'un fragment (8 caractères d'un dossier .content par ex.)
    QString findFullUuid(const QString &part) const;

private:
    StoryDb() = default;
    QJsonObject entry(const QUuid &u, bool *official = nullptr) const;
    void loadThirdParty();

    mutable QMutex m_lock;
    QHash<QString, QJsonObject> m_official;   // clé : UUID canonique
    QHash<QString, QJsonObject> m_thirdParty;
};

} // namespace lunii
