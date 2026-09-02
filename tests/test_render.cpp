#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QFile>
#include <QPainter>
#include <QTemporaryDir>

#include "render/foliopainter.h"
#include "render/pdfexport.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;
using Catch::Matchers::WithinAbs;

namespace {

// Proportion de pixels qui ne sont pas du blanc de feuille : mesure grossiere
// mais suffisante de « il y a quelque chose de dessine ».
double inkRatio(const QImage &image)
{
    int inked = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 220)
                ++inked;
        }
    }
    return double(inked) / double(image.width() * image.height());
}

bool hasInkNear(const QImage &image, const QPointF &mmPoint, double pixelsPerMm, int radius = 3)
{
    const int cx = int(std::lround(mmPoint.x() * pixelsPerMm));
    const int cy = int(std::lround(mmPoint.y() * pixelsPerMm));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
                continue;
            if (qGray(image.pixel(x, y)) < 220)
                return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Le rendu produit une image aux dimensions de la feuille", "[render]")
{
    Project project;
    Folio *folio = project.addFolio(QStringLiteral("Essai"));
    folio->sheet = sheetFormatById(QStringLiteral("A3")); // 420 x 297 mm

    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), 4.0);
    CHECK(image.width() == 1680);
    CHECK(image.height() == 1188);
}

TEST_CASE("Un fil est trace a l'emplacement demande", "[render]")
{
    Project project;
    Folio *folio = project.addFolio();
    drawWire(folio, { QPointF(50, 100), QPointF(200, 100) });

    const double ppm = 4.0;
    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);

    CHECK(hasInkNear(image, QPointF(125, 100), ppm));
    // Rien ne doit apparaitre la ou rien n'a ete dessine.
    CHECK_FALSE(hasInkNear(image, QPointF(125, 150), ppm, 2));
}

TEST_CASE("Un symbole rend son graphisme et ses broches", "[render]")
{
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio();
    placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(100, 100),
                QStringLiteral("-K1"));

    const double ppm = 6.0;
    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);

    // Le corps est un rectangle non rempli de 5 mm de cote : c'est son bord
    // qui porte l'encre, pas son centre.
    CHECK(hasInkNear(image, QPointF(97.5, 100), ppm));
    CHECK(hasInkNear(image, QPointF(102.5, 100), ppm));
    CHECK_FALSE(hasInkNear(image, QPointF(100, 100), ppm, 2));
    // Les broches courent de 95 a 97,5 mm et de 102,5 a 105 mm.
    CHECK(hasInkNear(image, QPointF(96, 100), ppm, 2));
    CHECK(hasInkNear(image, QPointF(104, 100), ppm, 2));
}

TEST_CASE("Une definition manquante est marquee, pas passee sous silence", "[render]")
{
    Project project;
    Folio *folio = project.addFolio();
    // La bibliotheque ne connait pas ce symbole : un trou invisible dans un
    // schema est bien pire qu'une marque explicite.
    placeSymbol(project, folio, QStringLiteral("iec:fantome"), QPointF(100, 100));

    const double ppm = 6.0;
    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);
    CHECK(hasInkNear(image, QPointF(100, 100), ppm));
}

TEST_CASE("Le style d'impression retire la grille", "[render]")
{
    Project project;
    Folio *folio = project.addFolio();

    RenderStyle withGrid = RenderStyle::print();
    withGrid.showGrid = true;
    withGrid.grid = QColor(0, 0, 0);

    const QImage printed = PdfExport::renderFolio(project, *folio, RenderStyle::print(), 3.0);
    const QImage screened = PdfExport::renderFolio(project, *folio, withGrid, 3.0);

    // La feuille imprimee ne porte que le cadre et le cartouche.
    CHECK(inkRatio(printed) < inkRatio(screened));
}

TEST_CASE("Le cadre et le cartouche sont traces", "[render]")
{
    Project project;
    project.info.title = QStringLiteral("Armoire de pompage");
    project.info.client = QStringLiteral("Régie");
    Folio *folio = project.addFolio(QStringLiteral("Commande"));
    folio->number = QStringLiteral("3");

    const double ppm = 4.0;
    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);

    const QRectF frame = folio->frameRect();
    CHECK(hasInkNear(image, frame.topLeft(), ppm));
    CHECK(hasInkNear(image, frame.bottomRight(), ppm));

    const QRectF block = folio->titleBlockRect();
    CHECK(hasInkNear(image, block.topLeft(), ppm));
    // Le titre du projet est inscrit dans la premiere bande du cartouche.
    CHECK(hasInkNear(image, block.topLeft() + QPointF(12, 10), ppm, 6));
}

TEST_CASE("La hauteur de texte est respectee en millimetres", "[render]")
{
    QImage canvas(400, 200, QImage::Format_RGB32);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);

    const QRectF small = FolioPainter::textBoundsMm(painter, QPointF(0, 0),
                                                    QStringLiteral("ABC"), 2.5);
    const QRectF large = FolioPainter::textBoundsMm(painter, QPointF(0, 0),
                                                    QStringLiteral("ABC"), 5.0);
    painter.end();

    // La hauteur de capitale d'un texte cote ne doit dependre ni du zoom ni du
    // peripherique.
    CHECK_THAT(small.height() - small.height() + 2.5, WithinAbs(2.5, 1e-9));
    CHECK_THAT(large.width() / small.width(), WithinAbs(2.0, 0.01));
}

TEST_CASE("Le cadrage du texte deplace sa boite", "[render]")
{
    QImage canvas(400, 200, QImage::Format_RGB32);
    QPainter painter(&canvas);
    const QPointF at(50, 50);
    const QRectF left = FolioPainter::textBoundsMm(painter, at, QStringLiteral("Repère"), 3.0,
                                                   Primitive::Align::Left);
    const QRectF centre = FolioPainter::textBoundsMm(painter, at, QStringLiteral("Repère"), 3.0,
                                                     Primitive::Align::Center);
    const QRectF right = FolioPainter::textBoundsMm(painter, at, QStringLiteral("Repère"), 3.0,
                                                    Primitive::Align::Right);
    painter.end();

    CHECK_THAT(left.left(), WithinAbs(50.0, 1e-6));
    CHECK_THAT(centre.center().x(), WithinAbs(50.0, 1e-6));
    CHECK_THAT(right.right(), WithinAbs(50.0, 1e-6));
}

TEST_CASE("Le PDF est ecrit et contient un folio par page", "[render][pdf]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    Project project;
    project.info.title = QStringLiteral("Dossier d'essai");
    project.library.insert(twoPinDevice());
    for (int i = 0; i < 3; ++i) {
        Folio *folio = project.addFolio(QStringLiteral("Folio %1").arg(i + 1));
        folio->number = QString::number(i + 1);
        placeSymbol(project, folio, QStringLiteral("iec:device"), QPointF(80 + i * 20, 90));
        drawWire(folio, { QPointF(60, 140), QPointF(220, 140) });
    }

    const QString path = dir.filePath(QStringLiteral("dossier.pdf"));
    QString error;
    REQUIRE(PdfExport::write(path, project, {}, &error));
    CHECK(error.isEmpty());

    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    CHECK(content.startsWith("%PDF-"));
    CHECK(content.contains("%%EOF"));
    // Trois folios, donc trois objets Page.
    CHECK(content.count("/Type /Page\n") + content.count("/Type /Page ")
                  + content.count("/Type/Page") >= 3);
    CHECK(content.size() > 2000);
}

TEST_CASE("Un projet sans folio ne produit pas de PDF muet", "[render][pdf]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Project project;
    QString error;
    CHECK_FALSE(PdfExport::write(dir.filePath(QStringLiteral("vide.pdf")), project, {}, &error));
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("La selection et la mise en evidence changent la couleur", "[render]")
{
    Project project;
    Folio *folio = project.addFolio();
    Wire *wire = drawWire(folio, { QPointF(50, 100), QPointF(200, 100) });

    RenderStyle style = RenderStyle::print();
    style.showFrame = false;
    style.showTitleBlock = false;

    auto renderWith = [&](const QSet<QString> &selection) {
        QImage image(int(folio->sheet.width * 4), int(folio->sheet.height * 4),
                     QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.scale(4.0, 4.0);
        FolioPainter p(project, style);
        p.setSelection(selection);
        p.paint(painter, *folio);
        painter.end();
        return image.pixel(int(125 * 4), int(100 * 4));
    };

    const QRgb normal = renderWith({});
    const QRgb selected = renderWith({ wire->id() });
    CHECK(normal != selected);
    CHECK(qRed(selected) > qRed(normal)); // la selection vire a l'orange
}

TEST_CASE("Les carreaux tiennent la ou les points renoncent", "[render][grille]")
{
    // Le garde-fou de la grille compte des marques, et le nombre de marques
    // depend de l'aspect : les carreaux coutent la somme des deux directions,
    // les points leur produit. Un seul seuil calibre sur les points ferait
    // disparaitre des carreaux qu'on trace sans effort.
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio(QStringLiteral("Grille"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));

    RenderStyle style = RenderStyle::screen();
    style.showGrid = true;
    style.gridStep = 0.5; // ~840 x 594 points : bien au-dela du seuil

    auto render = [&](GridStyle kind) {
        RenderStyle used = style;
        used.gridStyle = kind;
        used.showFrame = false;
        used.showTitleBlock = false;
        QImage image(600, 420, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        // Une echelle qui met la feuille A3 dans l'image : la grille se
        // dessine en millimetres, comme a l'ecran.
        painter.scale(image.width() / folio->sheetRect().width(),
                      image.width() / folio->sheetRect().width());
        FolioPainter(project, used).paint(painter, *folio);
        painter.end();
        int marked = 0;
        for (int y = 0; y < image.height(); y += 2) {
            for (int x = 0; x < image.width(); x += 2) {
                if (image.pixel(x, y) != qRgb(255, 255, 255))
                    ++marked;
            }
        }
        return marked;
    };

    CHECK(render(GridStyle::Lines) > 0);
    CHECK(render(GridStyle::Dots) == 0); // renonce, et c'est voulu
}
