// Chargement et enregistrement des bibliotheques de symboles.
//
// Le format sur disque est un JSON par categorie. Il est lisible, diffable et
// modifiable a la main en cas de besoin, ce qui compte pour une donnee que les
// utilisateurs vont produire en continu.
#pragma once

#include "core/symbollibrary.h"

#include <QString>
#include <QStringList>

namespace dsn {

struct LibraryLoadReport {
    int filesRead = 0;
    int symbolsLoaded = 0;
    QStringList errors;

    bool ok() const { return errors.isEmpty(); }
    void merge(const LibraryLoadReport &other);
};

class LibraryStore
{
public:
    // Bibliotheque integree au binaire. Toujours disponible, meme sur un poste
    // sans aucun fichier installe.
    static LibraryLoadReport loadBuiltin(SymbolLibrary &library);

    static LibraryLoadReport loadFile(const QString &path, SymbolLibrary &library);
    static LibraryLoadReport loadDirectory(const QString &path, SymbolLibrary &library,
                                           bool recursive = true);

    // Enregistre un lot de definitions dans un fichier de categorie.
    static bool saveFile(const QString &path, const QList<const SymbolDefinition *> &definitions,
                         QString *error = nullptr);

    // Repertoires ou l'utilisateur depose ses propres symboles, dans l'ordre de
    // priorite croissante : le local ecrase l'integre.
    static QStringList userSearchPaths();
    static QString writableUserPath();

    // Charge l'integre puis les repertoires utilisateur.
    static LibraryLoadReport loadAll(SymbolLibrary &library);

    static QString builtinRoot() { return QStringLiteral(":/libraries"); }
};

} // namespace dsn
