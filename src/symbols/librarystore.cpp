#include "librarystore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

namespace dsn {

namespace {
constexpr auto kFormatTag = "dessins-symbol-library";
}

void LibraryLoadReport::merge(const LibraryLoadReport &other)
{
    filesRead += other.filesRead;
    symbolsLoaded += other.symbolsLoaded;
    errors += other.errors;
}

LibraryLoadReport LibraryStore::loadFile(const QString &path, SymbolLibrary &library)
{
    LibraryLoadReport report;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report.errors.append(QStringLiteral("%1 : lecture impossible (%2)")
                                     .arg(path, file.errorString()));
        return report;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        report.errors.append(QStringLiteral("%1 : JSON invalide a l'offset %2 (%3)")
                                     .arg(path)
                                     .arg(parseError.offset)
                                     .arg(parseError.errorString()));
        return report;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormatTag)) {
        report.errors.append(QStringLiteral("%1 : ce n'est pas une bibliotheque de symboles")
                                     .arg(path));
        return report;
    }

    ++report.filesRead;
    const QJsonArray symbols = root.value(QStringLiteral("symbols")).toArray();
    for (const QJsonValue &value : symbols) {
        const SymbolDefinition definition = SymbolDefinition::fromJson(value);
        if (!definition.isValid()) {
            report.errors.append(QStringLiteral("%1 : symbole sans identifiant, ignore").arg(path));
            continue;
        }
        library.insert(definition);
        ++report.symbolsLoaded;
    }
    return report;
}

LibraryLoadReport LibraryStore::loadDirectory(const QString &path, SymbolLibrary &library,
                                              bool recursive)
{
    LibraryLoadReport report;
    if (!QFileInfo::exists(path))
        return report;

    QDirIterator it(path, QStringList{ QStringLiteral("*.json") }, QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    QStringList files;
    while (it.hasNext())
        files.append(it.next());

    // Ordre stable : deux postes doivent charger la meme bibliotheque dans le
    // meme ordre, sinon un conflit d'identifiant se resout differemment.
    files.sort();
    for (const QString &file : std::as_const(files))
        report.merge(loadFile(file, library));
    return report;
}

LibraryLoadReport LibraryStore::loadBuiltin(SymbolLibrary &library)
{
    return loadDirectory(builtinRoot(), library, true);
}

QStringList LibraryStore::userSearchPaths()
{
    QStringList paths;
    const QStringList data =
            QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &base : data)
        paths.append(base + QStringLiteral("/libraries"));
    return paths;
}

QString LibraryStore::writableUserPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/libraries");
}

LibraryLoadReport LibraryStore::loadAll(SymbolLibrary &library)
{
    LibraryLoadReport report = loadBuiltin(library);
    // Les repertoires utilisateur passent apres : un symbole local du meme
    // identifiant remplace celui d'origine, ce qui est le comportement attendu
    // quand on corrige un symbole integre.
    const QStringList paths = userSearchPaths();
    for (const QString &path : paths)
        report.merge(loadDirectory(path, library, true));
    return report;
}

bool LibraryStore::saveFile(const QString &path,
                            const QList<const SymbolDefinition *> &definitions, QString *error)
{
    QJsonArray symbols;
    for (const SymbolDefinition *definition : definitions) {
        if (definition)
            symbols.append(definition->toJson());
    }

    QJsonObject root;
    root[QStringLiteral("format")] = QLatin1String(kFormatTag);
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("symbols")] = symbols;

    QDir().mkpath(QFileInfo(path).absolutePath());

    // QSaveFile : un plantage en cours d'ecriture ne doit pas laisser une
    // bibliotheque a moitie ecrite a la place de l'ancienne.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace dsn
