#include "dsnfile.h"
#include "zip.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace dsn {

namespace {

constexpr auto kMimeEntry = "mimetype";
constexpr auto kProjectEntry = "project.json";
constexpr auto kLibraryEntry = "library.json";
constexpr auto kMetaEntry = "meta.json";

QByteArray encode(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QJsonObject decode(const QByteArray &data, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("JSON invalide a l'offset %1 : %2")
                             .arg(parseError.offset)
                             .arg(parseError.errorString());
        return {};
    }
    return document.object();
}

QJsonObject libraryToJson(const SymbolLibrary &library)
{
    QJsonArray symbols;
    const QList<const SymbolDefinition *> definitions = library.all();
    for (const SymbolDefinition *definition : definitions)
        symbols.append(definition->toJson());

    QJsonObject object;
    object[QStringLiteral("format")] = QStringLiteral("dessins-symbol-library");
    object[QStringLiteral("version")] = 1;
    object[QStringLiteral("symbols")] = symbols;
    return object;
}

int libraryFromJson(const QJsonObject &object, SymbolLibrary &library)
{
    int count = 0;
    const QJsonArray symbols = object.value(QStringLiteral("symbols")).toArray();
    for (const QJsonValue &value : symbols) {
        const SymbolDefinition definition = SymbolDefinition::fromJson(value);
        if (!definition.isValid())
            continue;
        library.insert(definition);
        ++count;
    }
    return count;
}

} // namespace

QString DsnFile::fileFilter()
{
    return QStringLiteral("Projet Dessins (*.dsn)");
}

QByteArray DsnFile::toArchive(const Project &project)
{
    ZipWriter writer;

    // Le type de contenu vient en premier, comme dans ODF : un outil peut
    // identifier le fichier en lisant ses premiers octets.
    writer.addFile(QLatin1String(kMimeEntry), mimeType().toUtf8());

    QJsonObject meta;
    meta[QStringLiteral("container")] = kContainerVersion;
    meta[QStringLiteral("document")] = Project::kFormatVersion;
    meta[QStringLiteral("producer")] =
            QStringLiteral("Dessins ") + QCoreApplication::applicationVersion();
    meta[QStringLiteral("created")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    writer.addFile(QLatin1String(kMetaEntry), encode(meta));

    writer.addFile(QLatin1String(kProjectEntry), encode(project.toJson()));
    writer.addFile(QLatin1String(kLibraryEntry), encode(libraryToJson(project.library)));

    return writer.archive();
}

DsnLoadResult DsnFile::fromArchive(const QByteArray &archive, Project &project)
{
    DsnLoadResult result;

    // Un .dsn est un ZIP, mais un JSON nu reste accepte : c'est ce que produit
    // une recuperation manuelle apres un incident, et le refuser serait punir
    // l'utilisateur au pire moment.
    if (!archive.startsWith("PK\x03\x04")) {
        QString error;
        const QJsonObject document = decode(archive, &error);
        if (document.isEmpty()) {
            result.error = error.isEmpty()
                    ? QStringLiteral("Le fichier n'est ni une archive Dessins ni un JSON valide.")
                    : error;
            return result;
        }
        if (!project.readJson(document)) {
            result.error = QStringLiteral(
                    "Document ecrit par une version plus recente du logiciel.");
            return result;
        }
        result.warnings.append(
                QStringLiteral("Fichier JSON nu : la bibliotheque embarquee est absente."));
        result.ok = true;
        result.formatVersion = document.value(QStringLiteral("version")).toInt(0);
        result.missingDefinitions = project.missingDefinitions();
        return result;
    }

    ZipReader reader(archive);
    if (!reader.isValid()) {
        result.error = reader.error();
        return result;
    }

    if (!reader.contains(QLatin1String(kProjectEntry))) {
        result.error = QStringLiteral("Archive incomplete : %1 est absent.")
                               .arg(QLatin1String(kProjectEntry));
        return result;
    }

    if (reader.contains(QLatin1String(kMetaEntry))) {
        QString error;
        const QJsonObject meta = decode(reader.read(QLatin1String(kMetaEntry)), &error);
        result.formatVersion = meta.value(QStringLiteral("document")).toInt(0);
        result.producer = meta.value(QStringLiteral("producer")).toString();
        if (meta.value(QStringLiteral("container")).toInt(0) > kContainerVersion) {
            result.error = QStringLiteral(
                    "Conteneur ecrit par une version plus recente du logiciel.");
            return result;
        }
    }

    // La bibliotheque se charge avant le projet : les instances peuvent alors
    // resoudre leur boite englobante des la lecture.
    if (reader.contains(QLatin1String(kLibraryEntry))) {
        QString error;
        const QJsonObject library = decode(reader.read(QLatin1String(kLibraryEntry)), &error);
        if (library.isEmpty() && !error.isEmpty())
            result.warnings.append(QStringLiteral("Bibliotheque embarquee illisible : ") + error);
        else
            result.symbolsEmbedded = libraryFromJson(library, project.library);
    } else {
        result.warnings.append(QStringLiteral("Aucune bibliotheque embarquee dans l'archive."));
    }

    QString error;
    const QJsonObject document = decode(reader.read(QLatin1String(kProjectEntry)), &error);
    if (document.isEmpty()) {
        result.error = error.isEmpty() ? QStringLiteral("Document vide.") : error;
        return result;
    }
    if (!project.readJson(document)) {
        result.error = QStringLiteral("Document ecrit par une version plus recente du logiciel.");
        return result;
    }

    project.resolveSymbolBounds();
    result.missingDefinitions = project.missingDefinitions();
    if (!result.missingDefinitions.isEmpty()) {
        result.warnings.append(QStringLiteral("%1 definition(s) de symbole introuvable(s).")
                                       .arg(result.missingDefinitions.size()));
    }
    result.ok = true;
    return result;
}

bool DsnFile::save(const QString &path, const Project &project, QString *error)
{
    // QSaveFile ecrit a cote puis renomme : une coupure de courant en cours
    // d'enregistrement laisse la version precedente intacte plutot qu'un
    // fichier tronque.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    const QByteArray archive = toArchive(project);
    if (file.write(archive) != archive.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

DsnLoadResult DsnFile::load(const QString &path, Project &project)
{
    DsnLoadResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("%1 : %2").arg(QFileInfo(path).fileName(), file.errorString());
        return result;
    }
    return fromArchive(file.readAll(), project);
}

} // namespace dsn
