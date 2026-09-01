// Format natif .arcus : un conteneur ZIP contenant du JSON.
//
// L'extension a change avec le nom du logiciel. Les dossiers enregistres en
// .dsn continuent de s'ouvrir : le contenu n'a pas bouge, et casser les
// fichiers de quelqu'un pour une question de marque serait indefendable.
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

    // Extension d'ecriture. La lecture accepte aussi l'ancienne, .dsn.
    static QString fileExtension() { return QStringLiteral("arcus"); }
    static QString legacyExtension() { return QStringLiteral("dsn"); }
    static QString fileFilter();
    // Type de contenu ecrit en tete de l'archive. Il n'est pas verifie a la
    // lecture : un dossier enregistre sous l'ancien nom s'ouvre tel quel.
    static QString mimeType() { return QStringLiteral("application/x-arcus-project"); }

    static constexpr int kContainerVersion = 1;
};

} // namespace dsn
