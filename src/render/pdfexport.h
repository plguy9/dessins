// Impression et export PDF.
//
// Le PDF n'a pas son propre moteur : il utilise FolioPainter, exactement comme
// l'ecran. C'est ce qui garantit que le dossier imprime correspond a ce qui a
// ete dessine — la promesse la plus facile a rompre dans un logiciel de CAO,
// et la plus couteuse a rompre.
#pragma once

#include "core/project.h"
#include "renderstyle.h"

#include <QImage>
#include <QList>
#include <QString>

namespace dsn {

struct PdfExportOptions {
    RenderStyle style = RenderStyle::print();
    // Vide : tous les folios, dans l'ordre du projet.
    QList<int> folioIndices;
    int resolution = 600; // points par pouce
    QString title;
    QString creator = QStringLiteral("Dessins");
};

class PdfExport
{
public:
    static bool write(const QString &path, const Project &project,
                      const PdfExportOptions &options = {}, QString *error = nullptr);

    // Rendu bitmap d'un folio. Sert a l'apercu, aux vignettes du navigateur de
    // folios, et a la verification automatisee du rendu sans ecran.
    static QImage renderFolio(const Project &project, const Folio &folio,
                              const RenderStyle &style = RenderStyle::print(),
                              double pixelsPerMm = 4.0);

    static bool writePng(const QString &path, const Project &project, const Folio &folio,
                         const RenderStyle &style = RenderStyle::print(),
                         double pixelsPerMm = 4.0, QString *error = nullptr);

    static QString fileFilter() { return QStringLiteral("Document PDF (*.pdf)"); }
};

} // namespace dsn
