// Plugins de sources audio (podcasts, vidéos…) : programmes externes décrits par un plugin.json,
// qui répondent en JSON. Le protocole est décrit dans PLUGINS.md.
#pragma once

#include <QAtomicInt>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

struct PluginInfo {
    QString id, name, version, description;
    QString dir;          // dossier du plugin
    QString executable;   // chemin complet
    bool search = false, link = false;
    QString searchHint, linkHint;
    QString icon;         // chemin complet, vide si absent

    // Dossiers parcourus : <appli>/plugins, <appli>/../share/easylunii/plugins, données utilisateur/plugins
    static QStringList searchPaths();
    static QList<PluginInfo> discover();
    static PluginInfo load(const QString &dir, QString *error = nullptr);
};

// Élément renvoyé par un plugin : un son, ou une collection (émission, playlist) à ouvrir
struct SourceItem {
    QString id, kind, title, subtitle, description, image, url, audioUrl, date, groupTitle;
    int duration = 0;   // secondes, 0 si inconnue
    int groupPart = 0;  // n° de partie dans une série, 0 si aucune
    QJsonObject raw;    // objet complet, renvoyé tel quel au plugin pour « fetch »

    bool isCollection() const { return kind == QLatin1String("collection"); }
    static SourceItem fromJson(const QJsonObject &o);
};

// Résultat local d'un téléchargement (dans le cache)
struct FetchedAudio {
    QString audio, image, title;
};

class PluginRunner
{
public:
    explicit PluginRunner(const PluginInfo &p) : m_p(p) {}

    using Progress = std::function<void(double fraction, const QString &message)>;
    // Bloquant (à appeler hors du thread de l'interface). Renvoie l'objet « result », vide si erreur.
    QJsonObject call(const QString &command, const QJsonObject &args, const Progress &progress = {},
                     const QAtomicInt *cancel = nullptr);
    QString error() const { return m_error; }

    QList<SourceItem> search(const QString &query);
    QList<SourceItem> open(const QString &url, QString *title = nullptr);
    // Téléchargement avec cache : un élément déjà récupéré n'est pas redemandé au plugin
    FetchedAudio fetch(const SourceItem &item, const Progress &progress = {}, const QAtomicInt *cancel = nullptr);

    static QString cacheDir(const QString &pluginId);
    static QString dataDir(const QString &pluginId);   // dossier inscriptible propre au plugin

private:
    PluginInfo m_p;
    QString m_error;
};
