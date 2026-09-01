#include <catch2/catch_test_macros.hpp>

#include "core/entities.h"
#include "core/folio.h"
#include "core/project.h"
#include "core/wiretype.h"
#include "io/dsnfile.h"
#include "io/dxfexport.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace dsn;

TEST_CASE("Un type de fil survit a l'aller-retour JSON", "[wiretype]")
{
    // Le type porte la couleur du conducteur : la perdre au ratissage du
    // fichier ferait rouvrir un dossier en monochrome.
    WireType t;
    t.id = QStringLiteral("l1");
    t.name = QStringLiteral("L1 — phase 1");
    t.rgb = 0x7A4A2Bu;
    t.width = 0.5;
    t.crossSection = QStringLiteral("2,5 mm²");
    t.layer = QStringLiteral("FILS_L1");
    t.style = WireStyle::dashed();

    const WireType back = WireType::fromJson(t.toJson());
    CHECK(back.id == t.id);
    CHECK(back.name == t.name);
    CHECK(back.rgb == 0x7A4A2Bu);
    CHECK(back.width == 0.5);
    CHECK(back.crossSection == t.crossSection);
    CHECK(back.layer == t.layer);
    CHECK(back.style == WireStyle::dashed());
    CHECK(back.colorName() == QLatin1String("#7a4a2b"));
}

TEST_CASE("Une couleur illisible laisse le type inchange", "[wiretype]")
{
    // Un fichier abime ne doit pas rendre un fil invisible : la couleur
    // precedente tient, elle est toujours tracable.
    WireType t;
    t.rgb = 0x123456u;
    t.setColorName(QStringLiteral("bleu"));
    CHECK(t.rgb == 0x123456u);
    t.setColorName(QStringLiteral("#00FF00"));
    CHECK(t.rgb == 0x00FF00u);
}

TEST_CASE("Un type inconnu retombe sur le type par defaut", "[wiretype]")
{
    // Un identifiant orphelin — type supprime, fichier d'une autre machine —
    // ne doit jamais faire disparaitre un fil du dessin.
    WireTypeSet set = WireTypeSet::forNorm(QStringLiteral("iec"));
    const WireType &fallback = set.resolve(QStringLiteral("inexistant"));
    CHECK(fallback.id == WireTypeSet::defaultId());
    CHECK(set.type(QStringLiteral("inexistant")) == nullptr);
}

TEST_CASE("Le type par defaut ne se supprime pas", "[wiretype]")
{
    // Il est le repli de tous les autres : sans lui, resolve() n'aurait plus
    // rien a rendre.
    WireTypeSet set = WireTypeSet::forNorm(QStringLiteral("iec"));
    const int before = set.count();
    set.remove(WireTypeSet::defaultId());
    CHECK(set.count() == before);
    CHECK(set.contains(WireTypeSet::defaultId()));

    set.remove(QStringLiteral("l1"));
    CHECK(set.count() == before - 1);
    CHECK_FALSE(set.contains(QStringLiteral("l1")));
}

TEST_CASE("Les jeux CEI et ANSI different par leurs couleurs", "[wiretype]")
{
    // Le brun de phase est une convention CEI ; l'usage nord-americain met du
    // noir. Confondre les deux serait un contresens de norme.
    const WireTypeSet iec = WireTypeSet::forNorm(QStringLiteral("iec"));
    const WireTypeSet ansi = WireTypeSet::forNorm(QStringLiteral("ANSI"));
    REQUIRE(iec.type(QStringLiteral("l1")));
    REQUIRE(ansi.type(QStringLiteral("l1")));
    CHECK(iec.type(QStringLiteral("l1"))->rgb != ansi.type(QStringLiteral("l1"))->rgb);
    // Le 24 V continu n'existe que dans le jeu CEI livre.
    CHECK(iec.contains(QStringLiteral("dc24")));
    CHECK_FALSE(ansi.contains(QStringLiteral("dc24")));
}

TEST_CASE("Les types de fils voyagent avec le projet", "[wiretype][io]")
{
    Project project;
    WireType t;
    t.id = QStringLiteral("secu");
    t.name = QStringLiteral("Sécurité");
    t.rgb = 0xE8A31Fu;
    t.crossSection = QStringLiteral("1 mm²");
    t.layer = QStringLiteral("FILS_SECU");
    t.style = WireStyle::dashDot();
    project.wireTypes.insert(t);

    Folio *folio = project.addFolio();
    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(10, 10), QPointF(60, 10) };
    wire->wireType = QStringLiteral("secu");
    folio->addEntity(std::move(wire));

    Project restored;
    const DsnLoadResult result = DsnFile::fromArchive(DsnFile::toArchive(project), restored);
    REQUIRE(result.ok);

    const WireType *back = restored.wireTypes.type(QStringLiteral("secu"));
    REQUIRE(back);
    CHECK(back->rgb == 0xE8A31Fu);
    CHECK(back->style == WireStyle::dashDot());
    CHECK(back->crossSection == QStringLiteral("1 mm²"));

    const Folio *restoredFolio = restored.folioAt(0);
    REQUIRE(restoredFolio);
    REQUIRE(restoredFolio->entityCount() == 1);
    const auto *restoredWire = dynamic_cast<const Wire *>(restoredFolio->entities().front().get());
    REQUIRE(restoredWire);
    CHECK(restoredWire->wireType == QLatin1String("secu"));
}

TEST_CASE("Un document anterieur aux types recoit le jeu de sa norme", "[wiretype][io]")
{
    // Les dossiers deja enregistres n'ont pas la cle « wireTypes ». Les ouvrir
    // avec un seul type par defaut appauvrirait le projet sans rien dire ;
    // on repart du jeu de la norme du profil.
    QJsonObject document;
    document[QStringLiteral("version")] = Project::kFormatVersion;
    document[QStringLiteral("profile")] = QStringLiteral("ansi");

    Project project;
    REQUIRE(project.readJson(document));
    CHECK(project.wireTypes.contains(QStringLiteral("l1")));
    CHECK_FALSE(project.wireTypes.contains(QStringLiteral("dc24"))); // jeu ANSI
}

TEST_CASE("Chaque type de fil devient un calque DXF", "[wiretype][dxf]")
{
    // C'est ce qui rend l'export exploitable : les potentiels arrivent deja
    // separes chez celui qui ouvre le fichier, comme le fait AutoCAD
    // Electrical.
    Project project;
    Folio *folio = project.addFolio(QStringLiteral("Puissance"));

    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(20, 30), QPointF(90, 30) };
    wire->wireType = QStringLiteral("l1");
    folio->addEntity(std::move(wire));

    const QString dxf = QString::fromUtf8(DxfExport::encodeFolio(project, *folio));
    CHECK(dxf.contains(QStringLiteral("\nFILS_L1\n")));
    // Le calque generique reste declare : un fil sans type y retombe.
    CHECK(dxf.contains(QStringLiteral("\nFILS\n")));
}

TEST_CASE("Un fil sans type reste sur le calque generique", "[wiretype][dxf]")
{
    Project project;
    Folio *folio = project.addFolio();
    auto wire = std::make_unique<Wire>();
    wire->points = { QPointF(20, 30), QPointF(90, 30) };
    folio->addEntity(std::move(wire));

    const QString dxf = QString::fromUtf8(DxfExport::encodeFolio(project, *folio));
    // La polyligne porte le calque FILS, pas un calque de type.
    const int entities = dxf.indexOf(QStringLiteral("\nENTITIES\n"));
    REQUIRE(entities > 0);
    CHECK(dxf.mid(entities).contains(QStringLiteral("\nFILS\n")));
    CHECK_FALSE(dxf.mid(entities).contains(QStringLiteral("\nFILS_L1\n")));
}
