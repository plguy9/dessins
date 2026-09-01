#include <catch2/catch_test_macros.hpp>

#include <QJsonDocument>
#include <QSet>

#include "io/csvexport.h"
#include "io/dsnfile.h"
#include "io/dxfexport.h"
#include "io/zip.h"
#include "symbols/librarystore.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

TEST_CASE("Une archive ZIP se relit entree par entree", "[io][zip]")
{
    ZipWriter writer;
    const QByteArray small("court");
    // Une charge repetitive, pour verifier que la compression est bien utilisee.
    const QByteArray large = QByteArray("le meme motif encore et encore. ").repeated(200);

    writer.addFile(QStringLiteral("petit.txt"), small);
    writer.addFile(QStringLiteral("dossier/gros.json"), large);
    writer.addFile(QStringLiteral("vide.txt"), QByteArray());

    const QByteArray archive = writer.archive();
    CHECK(archive.startsWith("PK\x03\x04"));
    // La compression doit avoir servi : sinon l'archive pesserait au moins
    // autant que la charge utile.
    CHECK(archive.size() < large.size());

    ZipReader reader(archive);
    REQUIRE(reader.isValid());
    CHECK(reader.entries().size() == 3);
    CHECK(reader.contains(QStringLiteral("dossier/gros.json")));
    CHECK(reader.read(QStringLiteral("petit.txt")) == small);
    CHECK(reader.read(QStringLiteral("dossier/gros.json")) == large);
    CHECK(reader.read(QStringLiteral("vide.txt")).isEmpty());
    CHECK(reader.read(QStringLiteral("absent.txt")).isEmpty());
}

TEST_CASE("Une archive tronquee est rejetee, pas devinee", "[io][zip]")
{
    ZipReader reader(QByteArray("PK\x03\x04 pas vraiment une archive"));
    CHECK_FALSE(reader.isValid());
    CHECK_FALSE(reader.error().isEmpty());
}

TEST_CASE("Un projet survit a l'aller-retour .dsn", "[io][dsn]")
{
    Project source;
    source.info.title = QStringLiteral("Poste de relevage");
    source.info.client = QStringLiteral("Régie municipale");
    source.info.reference = QStringLiteral("2026-031");
    source.profileId = QStringLiteral("iec");
    source.library.insert(twoPinDevice(QStringLiteral("contactor")));

    Folio *folio = source.addFolio(QStringLiteral("Commande"));
    folio->number = QStringLiteral("3");
    placeSymbol(source, folio, QStringLiteral("iec:contactor"), QPointF(60, 40),
                QStringLiteral("-K1"));
    Wire *wire = drawWire(folio, { QPointF(65, 40), QPointF(120, 40) });
    wire->number = QStringLiteral("305");

    const QByteArray archive = DsnFile::toArchive(source);

    Project restored;
    const DsnLoadResult result = DsnFile::fromArchive(archive, restored);
    REQUIRE(result.ok);
    CHECK(result.error.isEmpty());
    // La bibliotheque voyage avec le document : un dossier archive se rouvre
    // a l'identique meme si le poste n'a plus le symbole.
    CHECK(result.symbolsEmbedded == 1);
    CHECK(result.missingDefinitions.isEmpty());

    CHECK(restored.info.title == source.info.title);
    CHECK(restored.info.client == QStringLiteral("Régie municipale"));
    REQUIRE(restored.folioCount() == 1);
    CHECK(restored.folioAt(0)->number == QLatin1String("3"));
    CHECK(restored.folioAt(0)->entityCount() == 2);
    CHECK(restored.library.count() == 1);
}

TEST_CASE("Un JSON nu reste ouvrable, avec un avertissement", "[io][dsn]")
{
    Project source;
    source.addFolio(QStringLiteral("Unique"));
    const QByteArray json = QJsonDocument(source.toJson()).toJson();

    Project restored;
    const DsnLoadResult result = DsnFile::fromArchive(json, restored);
    // Refuser ce fichier serait punir l'utilisateur au pire moment.
    CHECK(result.ok);
    CHECK_FALSE(result.warnings.isEmpty());
    CHECK(restored.folioCount() == 1);
}

TEST_CASE("Un fichier illisible produit une erreur claire", "[io][dsn]")
{
    Project restored;
    const DsnLoadResult result = DsnFile::fromArchive(QByteArray("ceci n'est pas un projet"),
                                                      restored);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.isEmpty());
}

TEST_CASE("Le CSV échappe les champs et respecte le tableur local", "[io][csv]")
{
    ReportTable table;
    table.headers = { QStringLiteral("Repère"), QStringLiteral("Note"),
                      QStringLiteral("Longueur") };
    table.rows = { { QStringLiteral("-K1"), QStringLiteral("bobine; 24 V"),
                     QStringLiteral("12.5") },
                   { QStringLiteral("-K2"), QStringLiteral("dit \"principal\""),
                     QStringLiteral("3.0") } };

    const QByteArray csv = CsvExport::encode(table);
    const QString text = QString::fromUtf8(csv);

    // Marque d'octets : sans elle, Excel casse les accents a l'ouverture.
    CHECK(csv.startsWith("\xEF\xBB\xBF"));
    CHECK(text.contains(QStringLiteral("\"bobine; 24 V\"")));
    CHECK(text.contains(QStringLiteral("\"dit \"\"principal\"\"\"")));
    // Separateur decimal francais, sinon le tableur lit du texte.
    CHECK(text.contains(QStringLiteral("12,5")));
    CHECK(text.contains(QStringLiteral("\r\n")));
}

TEST_CASE("Le CSV anglo-saxon garde le point decimal", "[io][csv]")
{
    ReportTable table;
    table.headers = { QStringLiteral("Length") };
    table.rows = { { QStringLiteral("12.5") } };

    CsvOptions options;
    options.separator = QLatin1Char(',');
    options.decimalSeparator = QStringLiteral(".");
    options.byteOrderMark = false;

    const QString text = QString::fromUtf8(CsvExport::encode(table, options));
    CHECK(text.contains(QStringLiteral("12.5")));
    CHECK_FALSE(text.contains(QStringLiteral("12,5")));
}

TEST_CASE("Le DXF produit un fichier R12 structure", "[io][dxf]")
{
    Project project;
    project.library.insert(twoPinDevice(QStringLiteral("contactor")));
    Folio *folio = project.addFolio(QStringLiteral("Puissance"));
    folio->number = QStringLiteral("1");
    placeSymbol(project, folio, QStringLiteral("iec:contactor"), QPointF(60, 40),
                QStringLiteral("-K1"));
    Wire *wire = drawWire(folio, { QPointF(65, 40), QPointF(120, 40) });
    wire->number = QStringLiteral("101");

    const QString dxf = QString::fromUtf8(DxfExport::encodeFolio(project, *folio));

    CHECK(dxf.contains(QStringLiteral("AC1009")));      // R12
    CHECK(dxf.contains(QStringLiteral("\nSECTION\n")));
    CHECK(dxf.contains(QStringLiteral("\nBLOCKS\n")));
    CHECK(dxf.contains(QStringLiteral("\nENTITIES\n")));
    CHECK(dxf.endsWith(QStringLiteral("EOF\n")));
    // Les calques separent fils, symboles et reperes : sans cela le fichier
    // est inexploitable une fois ouvert ailleurs.
    CHECK(dxf.contains(QStringLiteral("\nFILS\n")));
    CHECK(dxf.contains(QStringLiteral("\nSYMBOLES\n")));
    CHECK(dxf.contains(QStringLiteral("\nREPERES\n")));
    CHECK(dxf.contains(QStringLiteral("\nINSERT\n")));
    CHECK(dxf.contains(QStringLiteral("\nPOLYLINE\n")));
    CHECK(dxf.contains(QStringLiteral("\n-K1\n")));
    CHECK(dxf.contains(QStringLiteral("\n101\n")));
    // R12 : pas de LWPOLYLINE, qui n'apparait qu'a partir de R14.
    CHECK_FALSE(dxf.contains(QStringLiteral("LWPOLYLINE")));
}

TEST_CASE("Le renversement d'axe place le dessin dans le repere DXF", "[io][dxf]")
{
    Project project;
    Folio *folio = project.addFolio();
    folio->sheet = sheetFormatById(QStringLiteral("A3")); // 420 x 297
    // Un fil horizontal a 40 mm du haut de la feuille doit ressortir a
    // 257 mm du bas dans le repere DXF.
    drawWire(folio, { QPointF(50, 40), QPointF(100, 40) });

    const QString dxf = QString::fromUtf8(DxfExport::encodeFolio(project, *folio));
    CHECK(dxf.contains(QStringLiteral("\n257.0000\n")));
    CHECK_FALSE(dxf.contains(QStringLiteral("\n40.0000\n")));
}

TEST_CASE("Les noms de bloc sont assainis pour le DXF", "[io][dxf]")
{
    CHECK(DxfExport::sanitizeName(QStringLiteral("iec:contact-no"))
          == QLatin1String("IEC_CONTACT-NO"));
    CHECK(DxfExport::sanitizeName(QStringLiteral("bobine temporisée"))
          == QLatin1String("BOBINE_TEMPORIS_E"));
    // Un nom de bloc R12 ne peut pas commencer par un chiffre.
    CHECK(DxfExport::sanitizeName(QStringLiteral("3phase")).startsWith(QLatin1Char('B')));
    CHECK_FALSE(DxfExport::sanitizeName(QString()).isEmpty());
}

TEST_CASE("La bibliotheque integree est embarquee dans le binaire", "[symbols]")
{
    SymbolLibrary library;
    const LibraryLoadReport report = LibraryStore::loadBuiltin(library);

    INFO("erreurs : " << report.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(report.errors.isEmpty());
    CHECK(report.filesRead > 0);
    // Le logiciel doit demarrer avec ses symboles sur un poste ou rien n'est
    // installe a cote.
    CHECK(report.symbolsLoaded >= 60);
    CHECK(library.count() == report.symbolsLoaded);

    // Les deux normes sont presentes et commutables.
    REQUIRE(library.definition(QStringLiteral("iec:contact-no")));
    REQUIRE(library.definition(QStringLiteral("ansi:contact-no")));
    const SymbolDefinition *swapped =
            library.counterpart(QStringLiteral("iec:contact-no"), QStringLiteral("ANSI"));
    REQUIRE(swapped);
    CHECK(swapped->id == QLatin1String("ansi:contact-no"));

    // Un symbole integre doit etre exploitable : des broches et un graphisme.
    const SymbolDefinition *coil = library.definition(QStringLiteral("iec:coil"));
    REQUIRE(coil);
    CHECK(coil->pins.size() == 2);
    CHECK(coil->pin(QStringLiteral("A1")) != nullptr);
    CHECK_FALSE(coil->graphics.isEmpty());
    CHECK(coil->designationPrefix == QLatin1String("K"));
    CHECK_FALSE(coil->bounds().isNull());
}

TEST_CASE("Tous les symboles integres sont coherents", "[symbols]")
{
    SymbolLibrary library;
    LibraryStore::loadBuiltin(library);

    QStringList problems;
    const auto definitions = library.all();
    for (const SymbolDefinition *definition : definitions) {
        if (definition->name.isEmpty())
            problems.append(definition->id + QStringLiteral(" : sans nom"));
        if (definition->category.isEmpty())
            problems.append(definition->id + QStringLiteral(" : sans categorie"));
        if (definition->pins.isEmpty())
            problems.append(definition->id + QStringLiteral(" : sans broche"));
        if (definition->bounds().isNull())
            problems.append(definition->id + QStringLiteral(" : boite vide"));

        QSet<QString> pinNumbers;
        for (const Pin &pin : definition->pins) {
            if (pin.number.isEmpty())
                problems.append(definition->id + QStringLiteral(" : broche sans repere"));
            // Deux broches de meme repere rendraient la netlist ambigue.
            if (pinNumbers.contains(pin.number))
                problems.append(definition->id + QStringLiteral(" : repere de broche en double : ")
                                + pin.number);
            pinNumbers.insert(pin.number);
        }
    }
    INFO(problems.join(QStringLiteral("\n")).toStdString());
    CHECK(problems.isEmpty());
}
