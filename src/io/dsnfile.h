// Format natif .dsn : un conteneur ZIP contenant du JSON.
//
// Le choix du conteneur suit celui d'ODF ou de KiCad, et pour les memes
// raisons : les symboles voyagent avec le document, la version du format est
// inscrite dedans, et un fichier abime reste inspectable avec un simple outil
// d'archivage plutot que perdu.
#pragma once

#include "core/project.h"

#include <QString>
#include <QStringList>

namespace dsn {

struct DsnLoadResult {
    bool ok = false;
    QString error;
    QStringList warnings;
    int formatVersion = 0;
    QString producer;      // version du logiciel ayant ecrit le fichier
    int symbolsEmbedded = 0;
    QStringList missingDefinitions;

    explicit operator bool() const { return ok; }
};

class DsnFile
{
public:
    // Ecrit le projet et sa bibliotheque embarquee. L'ecriture passe par un
    // fichier temporaire : une coupure en cours d'enregistrement ne doit pas
    // detruire la version precedente.
    static bool save(const QString &path, const Project &project, QString *error = nullptr);

    static DsnLoadResult load(const QString &path, Project &project);

    // Serialisation en memoire, utilisee par les tests et le presse-papiers.
    static QByteArray toArchive(const Project &project);
    static DsnLoadResult fromArchive(const QByteArray &archive, Project &project);

    static QString fileExtension() { return QStringLiteral("dsn"); }
    static QString fileFilter();
    static QString mimeType() { return QStringLiteral("application/x-dessins-project"); }

    static constexpr int kContainerVersion = 1;
};

} // namespace dsn
