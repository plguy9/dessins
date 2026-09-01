// Export DXF.
//
// Cible : DXF R12 (AC1009). C'est la variante la plus universellement lue —
// AutoCAD, LibreCAD, QCAD, les visionneuses — et la seule qui n'exige ni
// poignees ni marqueurs de sous-classe, donc la moins susceptible d'etre
// refusee par un lecteur strict.
//
// Ce qui traverse le DXF : la geometrie, les calques, les blocs, les textes.
// Ce qui ne traverse pas : la connectivite, les potentiels, les broches. Le
// DXF est un echange graphique, pas un format de schema, et l'interface doit
// le dire plutot que de laisser croire a un aller-retour sans perte.
#pragma once

#include "core/project.h"

#include <QString>
#include <QStringList>

namespace dsn {

struct DxfExportOptions {
    bool includeFrame = true;
    bool includeTitleBlock = true;
    bool includeWireNumbers = true;
    bool includeDesignations = true;
    bool includePinNumbers = false;
};

class DxfExport
{
public:
    static QByteArray encodeFolio(const Project &project, const Folio &folio,
                                  const DxfExportOptions &options = {});

    static bool writeFolio(const QString &path, const Project &project, const Folio &folio,
                           const DxfExportOptions &options = {}, QString *error = nullptr);

    // Un DXF ne contient qu'un espace objet : un projet multi-folios produit
    // un fichier par folio.
    static int writeProject(const QString &directory, const QString &baseName,
                            const Project &project, const DxfExportOptions &options = {},
                            QStringList *errors = nullptr);

    static QString fileFilter() { return QStringLiteral("Dessin AutoCAD (*.dxf)"); }

    // Les noms de bloc et de calque du R12 n'admettent ni accents, ni espaces,
    // ni deux-points.
    static QString sanitizeName(const QString &raw);
};

} // namespace dsn
