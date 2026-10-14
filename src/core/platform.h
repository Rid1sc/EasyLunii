// Petites différences entre systèmes (Linux, Windows).
#pragma once

#include <QString>
#include <QStringList>

namespace lunii {

// Vide les tampons d'écriture vers les disques (avant de débrancher une conteuse)
void flushDisks();
// Programme externe : à côté de l'exécutable, dans ~/.lunii-qt, puis dans le PATH (« .exe » ajouté sous Windows)
QString findTool(const QString &name, const QStringList &extraDirs = {});

} // namespace lunii
