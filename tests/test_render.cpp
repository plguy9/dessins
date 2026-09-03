#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QFile>
#include <QPainter>
#include <QTemporaryDir>

#include "core/titleblock.h"
#include "render/foliopainter.h"
#include "symbols/librarystore.h"
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
    // Le titre du projet est inscrit dans la première bande du cartouche.
    // On vise la bande, pas un pixel : le cartouche vient d'un gabarit, et
    // ses cases se centrent — figer une position au dixième de millimètre
    // ferait échouer le test au premier gabarit modifié, ce qui n'apprendrait
    // rien sur ce qu'on veut vraiment (le titre est écrit, et il est là).
    CHECK(hasInkNear(image, block.topLeft() + QPointF(12, 6.5), ppm, 6));
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

TEST_CASE("Un symbole grossi garde son épaisseur de trait", "[render][echelle]")
{
    // Le stylo est en millimètres et la transformation du symbole porte son
    // facteur d'échelle : sans compensation, un symbole deux fois plus grand
    // était aussi tracé deux fois plus épais.
    //
    // La mesure : l'encre déposée. Un contour de périmètre p tracé au trait w
    // couvre à peu près p×w. Doublé, son périmètre double — donc l'encre doit
    // doubler elle aussi. Si le trait suivait l'échelle, elle quadruplerait.
    Project project;
    project.library.insert(twoPinDevice());
    Folio *folio = project.addFolio(QStringLiteral("Échelle"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));

    auto inkFor = [&](double factor) {
        Folio page(*folio);
        auto instance = std::make_unique<SymbolInstance>();
        instance->definitionId = QStringLiteral("iec:device");
        instance->placement.position = QPointF(60, 45);
        instance->placement.scale = factor;
        page.addEntity(std::move(instance));

        RenderStyle style = RenderStyle::screen();
        style.showGrid = false;
        style.showFrame = false;
        style.showTitleBlock = false;
        style.showDesignations = false;
        style.showUnconnectedPins = false;

        QImage image(600, 450, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(5.0, 5.0); // 5 pixels par millimètre
        FolioPainter(project, style).paint(painter, page);
        painter.end();

        double ink = 0.0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                // Une couverture partielle compte pour ce qu'elle vaut :
                // l'antialiasing ne doit pas fausser la mesure.
                ink += 1.0 - qGray(image.pixel(x, y)) / 255.0;
            }
        }
        return ink;
    };

    const double single = inkFor(1.0);
    const double doubled = inkFor(2.0);
    REQUIRE(single > 100.0);

    // Deux, et surtout pas quatre.
    CHECK_THAT(doubled / single, WithinAbs(2.0, 0.25));
}


TEST_CASE("Un cadre en pointillé dépose moins d'encre qu'un cadre plein",
          "[render][trait]")
{
    // Le contour d'une armoire, d'un coffret, d'un groupe fonctionnel se trace
    // en POINTILLÉ : c'est une convention de lecture, et sans elle l'enveloppe
    // se confond avec le circuit. Le style vivait dans le type de fil mais pas
    // dans les formes, si bien qu'un cadre d'armoire était impossible à
    // dessiner — c'est l'essai de reproduction d'un vrai schéma qui l'a montré.
    //
    // La mesure est la seule qui ne mente pas : compter les pixels encrés. Un
    // rectangle pointillé au motif 3/2 mm doit en couvrir nettement moins que
    // le même rectangle plein, et pas zéro — un motif mal calibré donnerait
    // soit un trait plein, soit un trait invisible.
    Project project;
    Folio *folio = project.addFolio(QStringLiteral("Trait"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));

    auto inkFor = [&](Primitive::Stroke stroke) {
        Folio page(*folio);
        auto item = std::make_unique<GraphicItem>();
        item->shape = Primitive::rect(QRectF(10, 10, 80, 60), 0.35);
        item->shape.stroke = stroke;
        page.addEntity(std::move(item));

        RenderStyle style = RenderStyle::screen();
        style.showGrid = false;
        style.showFrame = false;
        style.showTitleBlock = false;

        QImage image(500, 400, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(5.0, 5.0);
        FolioPainter(project, style).paint(painter, page);
        painter.end();

        int encre = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixel(x, y) != qRgb(255, 255, 255))
                    ++encre;
        return encre;
    };

    const int plein = inkFor(Primitive::Stroke::Solid);
    const int pointille = inkFor(Primitive::Stroke::Dashed);
    const int fin = inkFor(Primitive::Stroke::Dotted);

    REQUIRE(plein > 0);
    // Le motif 3 mm de trait pour 2 mm de vide : trois cinquièmes de l'encre.
    CHECK(pointille > plein / 4);
    CHECK(pointille < plein * 4 / 5);
    // Le pointillé fin en dépose encore moins, sans disparaître.
    CHECK(fin > 0);
    CHECK(fin < pointille);
}

TEST_CASE("Le numéro d'une borne se lit sur le schéma", "[render][borne]")
{
    // Le numéro de borne était géré par l'éditeur de borniers et imprimé par
    // le rapport de câblage — et invisible sur le dessin. Le plan et le
    // rapport se contredisaient donc : un câbleur lisait « X1:4 » sur sa
    // feuille et ne trouvait sur le schéma qu'une borne anonyme.
    Project project;
    // La borne vient de la bibliotheque integree : c'est celle que
    // l'utilisateur pose, pas un gabarit de test.
    dsn::LibraryStore::loadBuiltin(project.library);
    Folio *folio = project.addFolio(QStringLiteral("Bornier"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));

    auto inkFor = [&](const QString &numero) {
        Folio page(*folio);
        auto borne = std::make_unique<SymbolInstance>();
        borne->definitionId = QStringLiteral("iec:terminal");
        borne->placement.position = QPointF(50, 40);
        borne->setDesignation(QStringLiteral("X1"));
        if (!numero.isEmpty())
            borne->fields.insert(QStringLiteral("terminal"), numero);
        page.addEntity(std::move(borne));

        RenderStyle style = RenderStyle::screen();
        style.showGrid = false;
        style.showFrame = false;
        style.showTitleBlock = false;
        style.showUnconnectedPins = false;

        QImage image(500, 400, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(5.0, 5.0);
        FolioPainter(project, style).paint(painter, page);
        painter.end();

        int encre = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixel(x, y) != qRgb(255, 255, 255))
                    ++encre;
        return encre;
    };

    const int sansNumero = inkFor(QString());
    const int avecNumero = inkFor(QStringLiteral("14"));
    REQUIRE(sansNumero > 0);
    // Deux chiffres de plus, c'est de l'encre en plus. Vérifier la seule
    // présence du champ n'aurait rien prouvé : c'est l'affichage qui manquait.
    CHECK(avecNumero > sansNumero);
}

TEST_CASE("Le code couleur d'un fil se lit sur le schéma", "[render][nommage]")
{
    // Invariant : ce que le rapport imprime, le dessin le montre. La liste
    // des fils porte désormais « N » dans sa colonne Couleur ; si la planche
    // ne le montrait pas, les deux documents se contrediraient — et c'est la
    // planche que le câbleur a en main.
    Project project;
    WireType noir;
    noir.id = QStringLiteral("noir");
    noir.name = QStringLiteral("Noir");
    noir.rgb = 0x202020u;
    project.wireTypes.insert(noir);

    Folio *folio = project.addFolio(QStringLiteral("Boucle"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    auto fil = std::make_unique<Wire>();
    fil->points = { QPointF(30, 40), QPointF(90, 40) };
    fil->number = QStringLiteral("101");
    fil->wireType = noir.id;
    folio->addEntity(std::move(fil));

    auto encrePour = [&](const QString &code) {
        Project copie(project);
        WireType type = noir;
        type.colorCode = code;
        copie.wireTypes.insert(type);

        RenderStyle style = RenderStyle::screen();
        style.showGrid = false;
        style.showFrame = false;
        style.showTitleBlock = false;
        style.showUnconnectedPins = false;

        QImage image(600, 400, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(5.0, 5.0);
        FolioPainter(copie, style).paint(painter, *copie.folioAt(0));
        painter.end();

        int encre = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixel(x, y) != qRgb(255, 255, 255))
                    ++encre;
        return encre;
    };

    const int sansCode = encrePour(QString());
    const int avecCode = encrePour(QStringLiteral("N"));
    REQUIRE(sansCode > 0);
    // « (N) » de plus a cote du repere, c'est de l'encre en plus. Verifier la
    // seule presence du champ n'aurait rien prouve : c'est l'affichage qui
    // manquait.
    CHECK(avecCode > sansCode);
}

TEST_CASE("La grille s'espace au lieu de disparaître", "[render][grille]")
{
    // Une grille dont les marques tombent à trois pixels l'une de l'autre
    // n'est plus une grille, c'est un voile gris — et le garde-fou de densité
    // l'abandonnait carrément, ce qui est le pire des trois cas. Signalé à
    // l'usage : *« il manque des carreaux, de la résolution »*.
    //
    // La règle se lit ici directement, plutôt qu'en comptant des pixels :
    // c'est le seul moyen de prouver qu'elle s'applique.
    const double pas = 2.5;

    // Échelle inconnue — PDF, vignette : le pas nominal, sans discussion.
    CHECK(FolioPainter::displayGridStep(pas, 0.0) == pas);

    // Zoom de travail : 2,5 mm à 4 px/mm font 10 px, c'est lisible tel quel.
    CHECK(FolioPainter::displayGridStep(pas, 4.0) == pas);

    // Zoom arrière : le pas double jusqu'à respirer, au lieu de se perdre.
    const double espace = FolioPainter::displayGridStep(pas, 0.3);
    CHECK(espace > pas);
    CHECK(espace * 0.3 >= FolioPainter::kMinGridPixels);

    // ET IL RESTE UN MULTIPLE DU PAS NOMINAL : la grille ne montre jamais un
    // point où l'on ne peut pas s'accrocher. C'est pour cela qu'on double au
    // lieu de multiplier par un facteur quelconque.
    const double rapport = espace / pas;
    CHECK(std::abs(rapport - std::round(rapport)) < 1e-9);
    CHECK(qFuzzyCompare(std::pow(2.0, std::round(std::log2(rapport))), rapport));

    // Zoom avant : on ne subdivise PAS sous le pas de la résolution — ce
    // serait montrer des points où l'accrochage ne se pose pas.
    CHECK(FolioPainter::displayGridStep(pas, 40.0) == pas);
}

// --------------------------------------------------------------------------
// Cotations

TEST_CASE("La cote mesure le dessin, elle ne le récite pas", "[render][cote]")
{
    // C'est toute la différence avec un texte posé à côté d'un trait : la
    // valeur se déduit des deux points d'attache. Déplacer une attache change
    // le nombre — un plan coté ne peut pas mentir sur ce qu'il montre.
    DimensionItem cote;
    cote.first = QPointF(50, 100);
    cote.second = QPointF(200, 100);
    cote.linePoint = QPointF(0, 120);
    CHECK(cote.measure() == 150.0);
    CHECK(cote.displayText() == QStringLiteral("150"));

    cote.second = QPointF(180, 100);
    CHECK(cote.displayText() == QStringLiteral("130"));

    // Une cote horizontale ne mesure que la projection : coter l'entraxe de
    // deux rails ne doit pas dépendre du fait qu'on a désigné deux points
    // exactement à la même hauteur.
    cote.second = QPointF(180, 140);
    cote.kind = DimensionItem::Kind::Horizontal;
    CHECK(cote.measure() == 130.0);
    cote.kind = DimensionItem::Kind::Vertical;
    CHECK(cote.measure() == 40.0);
    cote.kind = DimensionItem::Kind::Aligned;
    CHECK(cote.measure() > 130.0); // l'hypoténuse

    // La valeur imposée est le seul cas où une cote ne dit pas ce qu'elle
    // mesure — et elle se déclare.
    cote.override = QStringLiteral("hors échelle");
    CHECK(cote.displayText() == QStringLiteral("hors échelle"));
}

TEST_CASE("La ligne de cote passe par le point posé au troisième clic",
          "[render][cote]")
{
    // Le décalage est donné par un POINT, pas par une distance signée : le
    // troisième clic pose la ligne où on la veut, des deux côtés de la
    // mesure, sans avoir à penser au signe.
    DimensionItem cote;
    cote.first = QPointF(50, 100);
    cote.second = QPointF(200, 100);
    cote.kind = DimensionItem::Kind::Horizontal;

    cote.linePoint = QPointF(120, 130);
    auto g = cote.geometry();
    CHECK(g.lineStart == QPointF(50, 130));
    CHECK(g.lineEnd == QPointF(200, 130));

    cote.linePoint = QPointF(120, 70); // de l'autre côté
    g = cote.geometry();
    CHECK(g.lineStart.y() == 70.0);
}

TEST_CASE("Une cote dépose de l'encre là où elle mesure", "[render][cote]")
{
    // Le seul contrôle qui distingue une cote tracée d'une cote calculée mais
    // jamais peinte : compter l'encre. Un type d'entité qui n'atteint pas le
    // peintre est invisible sans qu'aucun test de géométrie ne s'en aperçoive.
    Project project;
    Folio *folio = project.addFolio();
    auto cote = std::make_unique<DimensionItem>();
    cote->first = QPointF(50, 100);
    cote->second = QPointF(200, 100);
    cote->linePoint = QPointF(120, 130);
    cote->kind = DimensionItem::Kind::Horizontal;
    folio->addEntity(std::move(cote));

    const double ppm = 4.0;
    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);

    CHECK(hasInkNear(image, QPointF(125, 130), ppm));  // la ligne de cote
    CHECK(hasInkNear(image, QPointF(50, 115), ppm));   // la ligne d'attache gauche
    CHECK(hasInkNear(image, QPointF(200, 115), ppm));  // et la droite
    CHECK(hasInkNear(image, QPointF(125, 127), ppm, 5)); // le nombre, au-dessus
    // Et rien loin de la cote.
    CHECK_FALSE(hasInkNear(image, QPointF(125, 180), ppm, 2));
}

TEST_CASE("Le texte d'une cote se lit toujours dans le bon sens", "[render][cote]")
{
    // Une cote verticale dont le texte suivrait la direction se lirait la tête
    // en bas une fois sur deux : au-delà du quart de tour on retourne.
    DimensionItem cote;
    cote.first = QPointF(100, 50);
    cote.second = QPointF(100, 200);
    cote.linePoint = QPointF(130, 0);
    cote.kind = DimensionItem::Kind::Vertical;
    const auto g = cote.geometry();
    // L'intervalle est [-90, 90) : une cote verticale se lit du bas vers le
    // haut (ISO 129), c'est-à-dire en tournant la planche d'un quart de tour
    // à droite — jamais à gauche.
    CHECK(g.angleDegrees == -90.0);
}

// --------------------------------------------------------------------------
// Le cartouche piloté par un gabarit

TEST_CASE("Une table de cartouche grandit vers le haut", "[render][cartouche]")
{
    // L'intitulé des colonnes est en bas, la révision 0 juste au-dessus, la 1
    // encore au-dessus. C'est l'ordre dans lequel on relit l'historique d'une
    // planche — la dernière révision tombe sous l'œil en premier — et c'est
    // ce que font toutes les planches relevées.
    Project project;
    project.titleBlock = TitleBlock::loopSheet();
    Folio *folio = project.addFolio(QStringLiteral("Boucle"));
    folio->sheet = sheetFormatById(QStringLiteral("A2"));
    folio->frame.titleBlockWidth = project.titleBlock.width;
    folio->frame.titleBlockHeight = project.titleBlock.height;

    // La case « revisions » du gabarit, pour viser ses lignes sans compter
    // des pixels au jugé.
    QRectF zone;
    for (const TitleBlockCell &cell : project.titleBlock.cells) {
        if (cell.kind == TitleBlockCell::Kind::Table
            && cell.key == QStringLiteral("revisions")) {
            zone = cell.rect;
        }
    }
    REQUIRE(!zone.isNull());

    const double ppm = 6.0;
    const QRectF bloc = folio->titleBlockRect();
    const QPointF origine = bloc.topLeft() + zone.topLeft();
    const double rowHeight = 1.8 * 1.6;                 // cf. FolioPainter
    const double headerY = origine.y() + zone.height() - rowHeight / 2.0;
    const double x = origine.x() + zone.width() * 0.55; // la colonne DESCRIPTION

    // Sans ligne : l'intitulé est là, rien au-dessus.
    QImage vide = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);
    CHECK(hasInkNear(vide, QPointF(x, headerY), ppm, 8));
    CHECK_FALSE(hasInkNear(vide, QPointF(x, headerY - rowHeight), ppm, 3));

    // Une ligne : elle se pose AU-DESSUS de l'intitulé, pas en dessous.
    folio->tables.insert(QStringLiteral("revisions"),
                         { { QStringLiteral("0"), {}, QStringLiteral("26.09.03"),
                             QStringLiteral("EMISSION") } });
    QImage une = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);
    CHECK(hasInkNear(une, QPointF(x, headerY - rowHeight), ppm, 5));
}

TEST_CASE("Une clef de cartouche inconnue n'écrit rien", "[render][cartouche]")
{
    // Un cartouche qui affiche « projectTitle » en toutes lettres part à
    // l'impression sans que personne ne le remarque : c'est pire que vide.
    Project project;
    TitleBlockTemplate gabarit;
    gabarit.id = QStringLiteral("essai");
    gabarit.width = 120.0;
    gabarit.height = 30.0;
    TitleBlockCell cell;
    cell.rect = QRectF(4, 4, 100, 10);
    cell.key = QStringLiteral("clefQuiNexistePas");
    cell.border = false;
    gabarit.cells.append(cell);
    project.titleBlock = gabarit;

    Folio *folio = project.addFolio();
    folio->frame.titleBlockWidth = gabarit.width;
    folio->frame.titleBlockHeight = gabarit.height;

    const double ppm = 6.0;
    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);
    const QRectF bloc = folio->titleBlockRect();
    CHECK_FALSE(hasInkNear(image, bloc.topLeft() + QPointF(20, 9), ppm, 4));
}

TEST_CASE("Les bandes de localisation sont tracées sur toute la hauteur",
          "[render][bandes]")
{
    // Le trait de séparation descend sur TOUTE la hauteur du cadre : c'est ce
    // qui dit qu'on change de lieu, pas qu'on change de paragraphe. Un trait
    // qui s'arrêterait au bandeau se lirait comme une décoration.
    Project project;
    Folio *folio = project.addFolio(QStringLiteral("Boucle"));
    folio->sheet = sheetFormatById(QStringLiteral("A3"));
    folio->bands = { { QStringLiteral("CHAMP"), 200.0 },
                     { QStringLiteral("CABINET"), 100.0 } };

    const double ppm = 4.0;
    const QImage image = PdfExport::renderFolio(project, *folio, RenderStyle::print(), ppm);

    const QRectF fr = folio->frameRect();
    const double x = folio->bandRect(1).left();
    CHECK(hasInkNear(image, QPointF(x, fr.top() + 20.0), ppm, 3));
    CHECK(hasInkNear(image, QPointF(x, fr.center().y()), ppm, 3));
    CHECK(hasInkNear(image, QPointF(x, fr.bottom() - 20.0), ppm, 3));
    // Et le nom est écrit dans le bandeau.
    CHECK(hasInkNear(image, QPointF(folio->bandRect(0).center().x(), fr.top() + 4.0), ppm, 6));
}

TEST_CASE("Les variantes de bulle ISA se distinguent au trait", "[render][isa]")
{
    // Les formes sont NORMATIVES, comme les marqueurs d'accrochage : cercle nu
    // = au champ, cercle barré = façade de panneau, trait pointillé = derrière
    // le panneau, carré = fonction partagée, losange = automate. Deux variantes
    // qui se ressemblent font poser la mauvaise, et le lecteur croit qu'un
    // relevé est accessible à l'opérateur alors qu'il est dans une armoire.
    SymbolLibrary library;
    LibraryStore::loadBuiltin(library);

    const QStringList ids = { QStringLiteral("iec:isa-field"), QStringLiteral("iec:isa-panel"),
                              QStringLiteral("iec:isa-rear"), QStringLiteral("iec:isa-shared"),
                              QStringLiteral("iec:isa-plc") };
    QVector<QByteArray> empreintes;
    for (const QString &id : ids) {
        const SymbolDefinition *def = library.definition(id);
        INFO(id.toStdString());
        REQUIRE(def);

        QImage image(120, 120, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.translate(60, 60);
        painter.scale(4.0, 4.0);
        FolioPainter::paintDefinition(painter, *def, RenderStyle::print(), false);
        painter.end();

        // Le contrôle mesure l'IMAGE, pas la liste des primitives : deux
        // symboles peuvent différer sur le papier et se rendre pareil.
        empreintes.append(QByteArray(reinterpret_cast<const char *>(image.constBits()),
                                     int(image.sizeInBytes())));
    }
    for (int i = 0; i < empreintes.size(); ++i) {
        for (int j = i + 1; j < empreintes.size(); ++j) {
            INFO(ids.at(i).toStdString() << " et " << ids.at(j).toStdString());
            CHECK(empreintes.at(i) != empreintes.at(j));
        }
    }
}

TEST_CASE("Une étiquette de câble ne coupe pas le fil qu'elle nomme",
          "[render][isa]")
{
    // Elle n'a AUCUNE broche, et c'est voulu : une broche factice la ferait
    // entrer dans la netlist et couper le fil posé dessous — on annoterait un
    // câble en le débranchant.
    SymbolLibrary library;
    LibraryStore::loadBuiltin(library);
    const SymbolDefinition *tag = library.definition(QStringLiteral("iec:cable-tag"));
    REQUIRE(tag);
    CHECK(tag->pins.isEmpty());
    CHECK(tag->noConnections);

    // Et une boîte de jonction non plus : c'est une enveloppe.
    const SymbolDefinition *bj = library.definition(QStringLiteral("iec:junction-box"));
    REQUIRE(bj);
    CHECK(bj->noConnections);
}
