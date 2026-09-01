// Rapports extraits du schema : nomenclature, bornier, liste des fils.
//
// Tout est deduit du document et de la netlist, jamais saisi deux fois. Un
// rapport qui diverge du schema est pire que pas de rapport du tout.
#pragma once

#include "core/netlist.h"
#include "core/project.h"
#include "profile.h"

namespace dsn {

// Tableau generique, rendu tel quel en CSV, en XLSX ou dans une vue.
struct ReportTable {
    QString title;
    QStringList headers;
    QVector<QStringList> rows;

    bool isEmpty() const { return rows.isEmpty(); }
    int rowCount() const { return int(rows.size()); }
};

struct BomLine {
    QString article;       // reference fabricant si connue, sinon identifiant de symbole
    QString name;
    QString value;
    QString manufacturer;
    QString partNumber;
    QStringList designations;
    QStringList folios;
    int quantity = 0;
};

struct TerminalLine {
    QString block;         // designation du bornier, ex. -X1
    QString terminal;      // repere de la borne
    QString folio;
    QString wireNumber;
    QString netName;
    QString target;        // appareil raccorde
    QString targetPin;
};

struct WireLine {
    QString number;
    QString netName;
    QStringList folios;
    QString from;
    QString fromPin;
    QString to;
    QString toPin;
    // Type de fil et section : c'est ce qu'on commande, la liste des fils
    // serait incomplete sans.
    QString wireTypeName;
    QString crossSection;
    int conductorCount = 1;
    double length = 0.0;   // somme des longueurs tracees, en millimetres
    int connectionCount = 0;
};

class Reports
{
public:
    // Regroupe les instances par article. Deux appareils de meme reference et
    // de meme valeur ne font qu'une ligne, avec la liste de leurs designations.
    static QVector<BomLine> billOfMaterials(const Project &project);

    // Bornier : une ligne par borne, avec le fil et l'appareil raccordes.
    static QVector<TerminalLine> terminalList(const Project &project, const Netlist &netlist);

    // Liste des fils : un potentiel par ligne, avec ses extremites.
    static QVector<WireLine> wireList(const Project &project, const Netlist &netlist);

    static ReportTable toTable(const QVector<BomLine> &lines);
    static ReportTable toTable(const QVector<TerminalLine> &lines);
    static ReportTable toTable(const QVector<WireLine> &lines);

    // Recapitulatif du projet, pour le panneau de controle.
    static ReportTable projectSummary(const Project &project, const Netlist &netlist);
    static ReportTable diagnostics(const Netlist &netlist);
};

} // namespace dsn
