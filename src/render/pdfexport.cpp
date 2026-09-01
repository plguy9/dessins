#include "pdfexport.h"
#include "foliopainter.h"

#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

namespace dsn {

namespace {

QPageLayout layoutFor(const Folio &folio)
{
    const QPageSize size(QSizeF(folio.sheet.width, folio.sheet.height), QPageSize::Millimeter,
                         folio.sheet.id, QPageSize::ExactMatch);
    // Marges nulles : le cadre et le cartouche font partie du dessin, ils ne
    // doivent pas etre repousses par une marge d'imprimante.
    return QPageLayout(size, QPageLayout::Portrait, QMarginsF(0, 0, 0, 0),
                       QPageLayout::Millimeter);
}

QList<int> resolveFolios(const Project &project, const PdfExportOptions &options)
{
    if (!options.folioIndices.isEmpty())
        return options.folioIndices;
    QList<int> all;
    for (int i = 0; i < project.folioCount(); ++i)
        all.append(i);
    return all;
}

} // namespace

bool PdfExport::write(const QString &path, const Project &project,
                      const PdfExportOptions &options, QString *error)
{
    const QList<int> folios = resolveFolios(project, options);
    if (folios.isEmpty()) {
        if (error)
            *error = QStringLiteral("Le projet ne contient aucun folio à imprimer.");
        return false;
    }

    QPdfWriter writer(path);
    writer.setResolution(options.resolution);
    writer.setCreator(options.creator);
    writer.setTitle(options.title.isEmpty() ? project.info.title : options.title);

    const Folio *first = project.folioAt(folios.first());
    if (!first) {
        if (error)
            *error = QStringLiteral("Folio introuvable.");
        return false;
    }
    writer.setPageLayout(layoutFor(*first));

    QPainter painter;
    if (!painter.begin(&writer)) {
        if (error)
            *error = QStringLiteral("Impossible d'écrire le PDF : %1").arg(QFileInfo(path).fileName());
        return false;
    }

    // Le peintre travaille en millimetres ; le PDF en points a la resolution
    // demandee. Une seule mise a l'echelle suffit a relier les deux.
    const double scale = options.resolution / kMmPerInch;

    FolioPainter folioPainter(project, options.style);
    bool firstPage = true;
    for (int index : folios) {
        const Folio *folio = project.folioAt(index);
        if (!folio)
            continue;
        if (!firstPage) {
            // La mise en page se change avant la page, pas apres : un projet
            // peut melanger A3 et A4.
            writer.setPageLayout(layoutFor(*folio));
            writer.newPage();
        }
        firstPage = false;

        painter.save();
        painter.scale(scale, scale);
        folioPainter.paint(painter, *folio);
        painter.restore();
    }

    painter.end();
    return true;
}

QImage PdfExport::renderFolio(const Project &project, const Folio &folio, const RenderStyle &style,
                              double pixelsPerMm)
{
    const int width = std::max(1, int(std::lround(folio.sheet.width * pixelsPerMm)));
    const int height = std::max(1, int(std::lround(folio.sheet.height * pixelsPerMm)));

    QImage image(width, height, QImage::Format_RGB32);
    image.fill(style.sheet);

    QPainter painter(&image);
    painter.scale(pixelsPerMm, pixelsPerMm);
    FolioPainter(project, style).paint(painter, folio);
    painter.end();
    return image;
}

bool PdfExport::writePng(const QString &path, const Project &project, const Folio &folio,
                         const RenderStyle &style, double pixelsPerMm, QString *error)
{
    const QImage image = renderFolio(project, folio, style, pixelsPerMm);
    if (!image.save(path, "PNG")) {
        if (error)
            *error = QStringLiteral("Impossible d'écrire l'image %1").arg(path);
        return false;
    }
    return true;
}

} // namespace dsn
