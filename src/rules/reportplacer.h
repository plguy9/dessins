// Poser un rapport dans le dessin, comme le « Put on Drawing » d'AutoCAD
// Electrical.
//
// Un rapport imprime a part se perd ; pose sur le folio, il voyage avec le
// dossier et se retrouve dans le PDF, le DXF et le tirage papier sans rien
// faire de plus. C'est la raison d'etre de cette piece.
//
// La table est faite d'entites ordinaires — traits et textes — et non d'un
// type « table » a part : elle se deplace, se copie et s'annule comme le
// reste du dessin, et le peintre n'a rien de nouveau a savoir.
#pragma once

#include "core/entity.h"
#include "reports.h"

#include <vector>

namespace dsn {

struct ReportTableSpec {
    QPointF origin{ 30.0, 40.0 };
    double textHeight = 2.0;
    double rowHeight = 5.0;
    double padding = 1.5;       // marge horizontale dans une cellule
    double lineWidth = 0.2;
    bool withTitle = true;
    bool withHeaders = true;

    // Decoupe en sections posees cote a cote, comme le fait AutoCAD quand un
    // rapport ne tient pas en hauteur. Zero = une seule section.
    int rowsPerSection = 0;
    double sectionGap = 8.0;

    // Largeurs de colonnes imposees, en millimetres. Le coeur ne charge pas
    // de police et ne peut qu'estimer ; la couche d'affichage, elle, sait
    // mesurer. Quand elle renseigne ce champ, la table est juste au pixel
    // pres au lieu d'etre juste a peu pres.
    QVector<double> explicitWidths;
};

class ReportPlacer
{
public:
    // Construit les entites de la table. Vide si le rapport n'a aucune ligne :
    // poser un cadre vide sur un folio n'apprend rien.
    static std::vector<EntityPtr> build(const ReportTable &table, const ReportTableSpec &spec);

    // Encombrement de la table sous ces reglages, sans la construire. Sert a
    // prevenir avant de poser quelque chose qui deborde de la feuille.
    static QRectF bounds(const ReportTable &table, const ReportTableSpec &spec);

    // Largeur d'une colonne, deduite du plus long contenu. Le coeur ne charge
    // pas de police : on reprend l'estimation de TextItem::boundingBox, celle
    // qui sert deja partout ailleurs.
    static QVector<double> columnWidths(const ReportTable &table, const ReportTableSpec &spec);
};

} // namespace dsn
