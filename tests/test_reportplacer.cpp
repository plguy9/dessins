#include <catch2/catch_test_macros.hpp>

#include "core/entities.h"
#include "rules/reportplacer.h"
#include "testhelpers.h"

using namespace dsn;
using namespace test;

namespace {

ReportTable smallTable(int rows = 3)
{
    ReportTable table;
    table.title = QStringLiteral("Liste des fils");
    table.headers = { QStringLiteral("Repère"), QStringLiteral("De"), QStringLiteral("Vers") };
    for (int i = 0; i < rows; ++i) {
        table.rows.append({ QStringLiteral("10%1").arg(i), QStringLiteral("-K%1").arg(i),
                            QStringLiteral("-Q%1").arg(i) });
    }
    return table;
}

int countOf(const std::vector<EntityPtr> &entities, EntityType type)
{
    int n = 0;
    for (const EntityPtr &e : entities) {
        if (e->type() == type)
            ++n;
    }
    return n;
}

} // namespace

TEST_CASE("Une table posee porte un texte par cellule remplie", "[reportplacer]")
{
    // La table est faite d'entites ordinaires : elle se deplace, se copie et
    // s'annule comme le reste du dessin.
    const ReportTable table = smallTable(3);
    const auto entities = ReportPlacer::build(table, {});

    // 3 intitules + 9 cellules + le titre.
    CHECK(countOf(entities, EntityType::Text) == 3 + 9 + 1);
    // Le quadrillage : 5 traits horizontaux (4 rangs + le bas) et 4 verticaux.
    CHECK(countOf(entities, EntityType::Graphic) == 5 + 4);
}

TEST_CASE("Un rapport vide ne pose rien", "[reportplacer]")
{
    // Poser un cadre vide sur un folio n'apprend rien a personne.
    ReportTable empty;
    empty.headers = { QStringLiteral("Repère") };
    CHECK(ReportPlacer::build(empty, {}).empty());
    CHECK(ReportPlacer::build(ReportTable(), {}).empty());
}

TEST_CASE("La largeur d'une colonne suit son plus long contenu", "[reportplacer]")
{
    ReportTable table;
    table.headers = { QStringLiteral("A"), QStringLiteral("B") };
    table.rows.append({ QStringLiteral("court"), QStringLiteral("un contenu bien plus long") });

    const QVector<double> widths = ReportPlacer::columnWidths(table, {});
    REQUIRE(widths.size() == 2);
    CHECK(widths.at(1) > widths.at(0));
    // L'intitule compte aussi : une colonne plus etroite que son titre serait
    // illisible.
    table.rows.clear();
    table.headers = { QStringLiteral("Un intitulé très long"), QStringLiteral("B") };
    const QVector<double> headerDriven = ReportPlacer::columnWidths(table, {});
    CHECK(headerDriven.at(0) > headerDriven.at(1));
}

TEST_CASE("Les sections se posent cote a cote", "[reportplacer]")
{
    // C'est ce que fait AutoCAD quand un rapport ne tient pas en hauteur :
    // il le coupe en colonnes plutot que de deborder de la feuille.
    const ReportTable table = smallTable(10);

    ReportTableSpec single;
    const QRectF tall = ReportPlacer::bounds(table, single);

    ReportTableSpec split;
    split.rowsPerSection = 5;
    const QRectF wide = ReportPlacer::bounds(table, split);

    CHECK(wide.height() < tall.height());
    CHECK(wide.width() > tall.width());
    // Deux sections repetent les intitules : deux fois trois de plus.
    CHECK(countOf(ReportPlacer::build(table, split), EntityType::Text)
          == countOf(ReportPlacer::build(table, single), EntityType::Text) + 3);
}

TEST_CASE("La table posee tient dans l'encombrement annonce", "[reportplacer]")
{
    // L'encombrement sert a prevenir avant de poser : il doit correspondre a
    // ce qui sera reellement dessine, sinon l'avertissement ne vaut rien.
    const ReportTable table = smallTable(6);
    ReportTableSpec spec;
    spec.origin = QPointF(30, 40);
    const QRectF box = ReportPlacer::bounds(table, spec);

    QRectF actual;
    for (const EntityPtr &entity : ReportPlacer::build(table, spec))
        actual = actual.isNull() ? entity->boundingBox() : actual.united(entity->boundingBox());

    // Les textes debordent legerement du quadrillage par leur estimation de
    // largeur ; l'encombrement doit rester du bon ordre de grandeur.
    CHECK(actual.width() <= box.width() * 1.15);
    CHECK(actual.height() <= box.height() * 1.15);
    CHECK(box.left() == 30.0);
}
