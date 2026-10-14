#include "core/studiopack.h"

#include "core/archive.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

namespace lunii {

static const int NODE_SIZE = 0x2C;
static const int NI_HEADER_SIZE = 0x200;

static Transition readTransition(const QJsonValue &v)
{
    Transition t;
    if (v.isObject()) {
        t.actionId = v.toObject().value("actionNode").toString();
        t.optionIndex = v.toObject().value("optionIndex").toInt();
    }
    return t;
}

static QJsonValue writeTransition(const Transition &t)
{
    if (!t.isSet())
        return QJsonValue::Null;
    return QJsonObject{{"actionNode", t.actionId}, {"optionIndex", t.optionIndex}};
}

static QPointF readPos(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    return {o.value("x").toDouble(), o.value("y").toDouble()};
}

StudioPack StudioPack::fromJson(const QJsonObject &root)
{
    static const QStringList stageKeys = {"uuid", "name", "type", "groupId", "position", "squareOne", "image",
                                          "audio", "okTransition", "homeTransition", "controlSettings"};
    static const QStringList actionKeys = {"id", "name", "type", "groupId", "position", "options"};
    static const QStringList rootKeys = {"format", "version", "title", "description", "nightModeAvailable",
                                         "stageNodes", "actionNodes"};
    StudioPack p;
    p.format = root.value("format").toString("v1");
    p.version = root.value("version").toInt(1);
    p.title = root.value("title").toString();
    p.description = root.value("description").toString();
    p.nightMode = root.value("nightModeAvailable").toBool(false);
    for (const auto &v : root.value("stageNodes").toArray()) {
        const QJsonObject o = v.toObject();
        StageNode n;
        n.uuid = o.value("uuid").toString();
        n.name = o.value("name").toString();
        n.type = o.value("type").toString();
        n.groupId = o.value("groupId").toString();
        n.pos = readPos(o.value("position"));
        n.image = o.value("image").toString();
        n.audio = o.value("audio").toString();
        n.ok = readTransition(o.value("okTransition"));
        n.home = readTransition(o.value("homeTransition"));
        const QJsonObject c = o.value("controlSettings").toObject();
        n.ctrl = {c.value("wheel").toBool(), c.value("ok").toBool(), c.value("home").toBool(),
                  c.value("pause").toBool(), c.value("autoplay").toBool()};
        for (auto it = o.begin(); it != o.end(); ++it)
            if (!stageKeys.contains(it.key()))
                n.extra.insert(it.key(), it.value());
        p.stages.append(n);
    }
    for (const auto &v : root.value("actionNodes").toArray()) {
        const QJsonObject o = v.toObject();
        ActionNode a;
        a.id = o.value("id").toString();
        a.name = o.value("name").toString();
        a.type = o.value("type").toString();
        a.groupId = o.value("groupId").toString();
        a.pos = readPos(o.value("position"));
        for (const auto &opt : o.value("options").toArray())
            a.options << opt.toString();
        for (auto it = o.begin(); it != o.end(); ++it)
            if (!actionKeys.contains(it.key()))
                a.extra.insert(it.key(), it.value());
        p.actions.append(a);
    }
    for (auto it = root.begin(); it != root.end(); ++it)
        if (!rootKeys.contains(it.key()))
            p.extra.insert(it.key(), it.value());
    return p;
}

QJsonObject StudioPack::toJson() const
{
    QJsonObject root = extra;
    root.insert("format", format);
    root.insert("title", title.isEmpty() ? QStringLiteral("MISSING_PACK_TITLE") : title);
    if (!description.isNull())
        root.insert("description", description);
    root.insert("version", version);
    root.insert("nightModeAvailable", nightMode);
    QJsonArray sn;
    for (int i = 0; i < stages.size(); ++i) {
        const StageNode &n = stages[i];
        QJsonObject o = n.extra;
        o.insert("uuid", n.uuid);
        o.insert("name", n.name);
        if (!n.type.isEmpty())
            o.insert("type", n.type);
        if (!n.groupId.isEmpty())
            o.insert("groupId", n.groupId);
        o.insert("position", QJsonObject{{"x", int(n.pos.x())}, {"y", int(n.pos.y())}});
        if (i == 0)
            o.insert("squareOne", true);
        o.insert("image", n.image.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(n.image));
        o.insert("audio", n.audio.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(n.audio));
        o.insert("okTransition", writeTransition(n.ok));
        o.insert("homeTransition", writeTransition(n.home));
        o.insert("controlSettings", QJsonObject{{"wheel", n.ctrl.wheel}, {"ok", n.ctrl.ok}, {"home", n.ctrl.home},
                                                {"pause", n.ctrl.pause}, {"autoplay", n.ctrl.autoplay}});
        sn.append(o);
    }
    root.insert("stageNodes", sn);
    QJsonArray an;
    for (const ActionNode &a : actions) {
        QJsonObject o = a.extra;
        o.insert("id", a.id);
        o.insert("name", a.name);
        if (!a.type.isEmpty())
            o.insert("type", a.type);
        if (!a.groupId.isEmpty())
            o.insert("groupId", a.groupId);
        o.insert("position", QJsonObject{{"x", int(a.pos.x())}, {"y", int(a.pos.y())}});
        o.insert("options", QJsonArray::fromStringList(a.options));
        an.append(o);
    }
    root.insert("actionNodes", an);
    return root;
}

QByteArray StudioPack::assetData(const QString &name) const
{
    if (auto it = assets.constFind(name); it != assets.constEnd())
        return *it;
    QFile f(assetFiles.value(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

qint64 StudioPack::assetSize(const QString &name) const
{
    if (auto it = assets.constFind(name); it != assets.constEnd())
        return it->size();
    return assetFiles.contains(name) ? QFileInfo(assetFiles.value(name)).size() : 0;
}

void StudioPack::putAsset(const QString &name, const QByteArray &data, const QString &spillDir)
{
    static const QStringList audio = {"mp3", "ogg", "oga", "wav", "m4a", "flac", "opus", "aac"};
    const bool spill = !spillDir.isEmpty()
                    && (audio.contains(QFileInfo(name).suffix().toLower()) || data.size() > 512 * 1024);
    if (spill) {
        const QString file = spillDir + "/" + name;
        QFile f(file);
        if (QFileInfo(file).size() == data.size() || (f.open(QIODevice::WriteOnly) && f.write(data) == data.size())) {
            assetFiles.insert(name, file);
            assets.remove(name);
            return;
        }
    }
    assets.insert(name, data);
}

bool StudioPack::loadArchive(const QString &path, StudioPack &out, QString *error, bool withAssets, const QString &spillDir)
{
    ArchiveReader ar(path);
    if (!ar.isValid()) {
        if (error)
            *error = ar.error();
        return false;
    }
    QByteArray json, thumb;
    StudioPack holder;
    const bool ok = ar.forEach(
        [&](const ArchiveEntry &e, const QByteArray &d) {
            if (e.name == "story.json")
                json = d;
            else if (e.name == "thumbnail.png")
                thumb = d;
            else if (e.name.startsWith("assets/"))
                holder.putAsset(e.name.mid(7), d, spillDir);
            return true;
        },
        [&](const QString &n) { return n == "story.json" || n == "thumbnail.png" || (withAssets && n.startsWith("assets/")); });
    if (!ok || json.isEmpty()) {
        if (error)
            *error = json.isEmpty() ? QStringLiteral("story.json absent") : ar.error();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    out = fromJson(doc.object());
    out.assets = std::move(holder.assets);
    out.assetFiles = std::move(holder.assetFiles);
    out.thumbnail = thumb;
    return true;
}

bool StudioPack::saveArchive(const QString &path, QString *error) const
{
    ArchiveWriter w(path, ArchiveWriter::Zip);
    if (!w.isOpen()) {
        if (error)
            *error = w.error();
        return false;
    }
    bool ok = w.add("story.json", QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!thumbnail.isEmpty())
        ok = ok && w.add("thumbnail.png", thumbnail);
    ok = ok && w.addDir("assets/");
    // seuls les assets réellement référencés sont écrits
    QStringList used;
    for (const StageNode &n : stages) {
        if (!n.image.isEmpty() && !used.contains(n.image))
            used << n.image;
        if (!n.audio.isEmpty() && !used.contains(n.audio))
            used << n.audio;
    }
    used.sort();
    for (const QString &a : used)
        ok = ok && w.add("assets/" + a, assetData(a));
    ok = w.close() && ok;
    if (!ok && error)
        *error = w.error();
    return ok;
}

QUuid StudioPack::packUuid() const
{
    return stages.isEmpty() ? QUuid() : QUuid(stages.first().uuid);
}

int StudioPack::stageIndex(const QString &uuid) const
{
    for (int i = 0; i < stages.size(); ++i)
        if (stages[i].uuid == uuid)
            return i;
    return -1;
}

int StudioPack::actionIndex(const QString &id) const
{
    for (int i = 0; i < actions.size(); ++i)
        if (actions[i].id == id)
            return i;
    return -1;
}

// Nom Lunii d'un asset : 8 derniers caractères du nom sans extension, en majuscules (comme Lunii.QT).
// En cas de collision (deux assets différents), repli sur un nom numéroté.
static void addAsset(const QString &asset, QStringList &order, QHash<QString, QString> &names,
                     QHash<QString, int> &pos)
{
    if (asset.isEmpty() || pos.contains(asset))
        return;
    QString base = QFileInfo(asset).completeBaseName().right(8).toUpper();
    const QList<QString> taken = names.values();
    if (taken.contains(base))
        base = QString("%1").arg(order.size(), 8, 16, QChar('0')).toUpper();
    pos.insert(asset, order.size());
    names.insert(asset, base);
    order << asset;
}

StudioPack::LuniiIndex StudioPack::luniiIndex() const
{
    LuniiIndex ix;
    for (const StageNode &n : stages) {
        addAsset(n.image, ix.riOrder, ix.riName, ix.riPos);
        addAsset(n.audio, ix.siOrder, ix.siName, ix.siPos);
    }
    int global = 0;
    for (const ActionNode &a : actions) {
        ix.actionGlobalIndex.insert(a.id, global);
        global += a.options.size();
        for (const QString &opt : a.options)
            ix.li << stageIndex(opt);
    }
    return ix;
}

QByteArray StudioPack::riData(const LuniiIndex &ix)
{
    QByteArray d;
    for (const QString &a : ix.riOrder)
        d += "000\\" + ix.riName.value(a).toLatin1();
    return d;
}

QByteArray StudioPack::siData(const LuniiIndex &ix)
{
    QByteArray d;
    for (const QString &a : ix.siOrder)
        d += "000\\" + ix.siName.value(a).toLatin1();
    return d;
}

QByteArray StudioPack::liData(const LuniiIndex &ix)
{
    QByteArray d;
    for (int v : ix.li) {
        char b[4];
        qToLittleEndian<qint32>(v, b);
        d.append(b, 4);
    }
    while (d.size() < 8)
        d.append('\0');
    return d;
}

static void put32(QByteArray &d, qint32 v)
{
    char b[4];
    qToLittleEndian<qint32>(v, b);
    d.append(b, 4);
}

static void put16(QByteArray &d, quint16 v)
{
    char b[2];
    qToLittleEndian<quint16>(v, b);
    d.append(b, 2);
}

QByteArray StudioPack::niData(const LuniiIndex &ix) const
{
    QByteArray d;
    bool okNum = false;
    const int fmt = format.mid(1).toInt(&okNum);
    put16(d, quint16(okNum ? fmt : 1));
    put16(d, quint16(version));
    put32(d, NI_HEADER_SIZE);
    put32(d, NODE_SIZE);
    put32(d, qint32(stages.size()));
    put32(d, qint32(ix.riOrder.size()));
    put32(d, qint32(ix.siOrder.size()));
    d.append(char(1));
    d.append(QByteArray(NI_HEADER_SIZE - d.size(), 0));

    auto trans = [&](QByteArray &n, const Transition &t) {
        const int ai = t.isSet() ? actionIndex(t.actionId) : -1;
        if (ai < 0) {
            n.append(QByteArray(12, char(0xFF)));
            return;
        }
        put32(n, ix.actionGlobalIndex.value(t.actionId));
        put32(n, qint32(actions[ai].options.size()));
        put32(n, t.optionIndex);
    };

    for (const StageNode &s : stages) {
        QByteArray n;
        put32(n, s.image.isEmpty() ? -1 : ix.riPos.value(s.image, -1));
        put32(n, s.audio.isEmpty() ? -1 : ix.siPos.value(s.audio, -1));
        trans(n, s.ok);
        trans(n, s.home);
        put16(n, s.ctrl.wheel);
        put16(n, s.ctrl.ok);
        put16(n, s.ctrl.home);
        put16(n, s.ctrl.pause);
        put16(n, s.ctrl.autoplay);
        put16(n, 0);
        n.append(QByteArray(NODE_SIZE - n.size(), char(0xAA)));
        d += n;
    }
    return d;
}

QStringList StudioPack::validate() const
{
    QStringList issues;
    if (stages.isEmpty())
        issues << QObject::tr("Le pack ne contient aucun nœud.");
    for (const StageNode &s : stages) {
        const QString who = s.name.isEmpty() ? s.uuid : s.name;
        for (const Transition *t : {&s.ok, &s.home}) {
            if (!t->isSet())
                continue;
            const int ai = actionIndex(t->actionId);
            if (ai < 0)
                issues << QObject::tr("« %1 » : transition vers un nœud d'action absent.").arg(who);
            else if (t->optionIndex >= actions[ai].options.size())
                issues << QObject::tr("« %1 » : option %2 hors limites.").arg(who).arg(t->optionIndex);
        }
        const bool haveAssets = !assets.isEmpty() || !assetFiles.isEmpty();
        if (!s.image.isEmpty() && haveAssets && !hasAsset(s.image))
            issues << QObject::tr("« %1 » : image %2 manquante.").arg(who, s.image);
        if (!s.audio.isEmpty() && haveAssets && !hasAsset(s.audio))
            issues << QObject::tr("« %1 » : audio %2 manquant.").arg(who, s.audio);
        if (s.ctrl.autoplay && !s.ok.isSet())
            issues << QObject::tr("« %1 » : lecture auto sans transition OK (la Lunii s'arrêtera).").arg(who);
    }
    for (const ActionNode &a : actions) {
        if (a.options.isEmpty())
            issues << QObject::tr("Nœud d'action « %1 » sans option.").arg(a.name.isEmpty() ? a.id : a.name);
        for (const QString &o : a.options)
            if (stageIndex(o) < 0)
                issues << QObject::tr("Nœud d'action « %1 » : option vers un nœud absent.").arg(a.name.isEmpty() ? a.id : a.name);
    }
    return issues;
}

} // namespace lunii
