// Rapports extraits du schema : nomenclature, bornier, liste des fils.
//
// Tout est deduit du document et de la netlist, jamais saisi deux fois. Un
// rapport qui diverge du schema est pire que pas de rapport du tout.
#pragma once

#include "core/netlist.h"
#include "core/project.h"
#include "plc.h"
#include "profile.h"

namespace dsn {

// Perimetre d'un rapport. AutoCAD Electrical commence chacun de ses rapports
// par cette question : tout le projet, ou le dessin actif seulement. C'est la
// difference entre verifier une page et sortir le dossier complet.
struct ReportScope {
    QString folioId;   // vide = tout le projet

    bool isProject() const { return folioId.isEmpty(); }
    bool includes(const QString &id) const { return folioId.isEmpty() || id == folioId; }
    // Un potentiel est retenu des qu'il touche le folio : une liaison qui
    // sort de la page reste une liaison de cette page.
    bool touches(const QStringList &folioIds) const
    {
        return folioId.isEmpty() || folioIds.contains(folioId);
    }
};

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

// Une liaison physique a cabler : un point de depart, un point d'arrivee, et
// le fil qui les relie. C'est la ligne du rapport De/Vers d'AutoCAD
// Electrical — celle qu'un cableur suit, fil apres fil.
struct WireRunLine {
    QString wireNumber;
    QString netName;

    QString fromDesignation;
    QString fromPin;
    QString fromFolio;
    QString fromZone;
    // La BANDE de localisation, quand le folio en porte : « CHAMP »,
    // « CABINET 037BJ0151 ». C'est ce qu'un cableur cherche en premier — pas
    // la zone du cadre, mais l'endroit physique ou il doit aller visser.
    QString fromLocation;

    QString toDesignation;
    QString toPin;
    QString toFolio;
    QString toZone;
    QString toLocation;

    QString wireTypeName;
    QString colorName;     // « #rrggbb », la couleur du type
    QString crossSection;

    // Vrai quand les deux extremites ne sont pas sur le meme folio : la
    // liaison passe alors par un renvoi, et se cable par un bornier.
    bool crossesFolios = false;
};

// Une ligne du rapport de composants : un appareil, ou qu'il soit pose.
struct ComponentLine {
    QString designation;
    QString family;        // categorie du symbole principal
    QString description;   // nom du symbole, ou champ « description » saisi
    QString value;
    QString manufacturer;
    QString partNumber;
    QString folio;
    QString zone;          // repere de zone du bloc principal, ex. C3
    QStringList folios;    // tous les folios ou l'appareil apparait
    int blockCount = 1;    // nombre de symboles poses pour cet appareil
    int pinCount = 0;      // broches raccordees
};

// Une ligne du rapport d'adresses et descriptions d'entrees-sorties
// d'automate : un point par ligne, avec ce qu'il commande ou ce qu'il lit.
// C'est le document que l'automaticien et le cableur partagent — l'un y lit
// l'adresse a programmer, l'autre la borne a raccorder.
struct PlcIoLine {
    QString designation;   // repere du module, ex. -A1
    QString module;        // reference constructeur
    QString manufacturer;
    QString ioType;        // entree-tor, sortie-tor, entree-analogique...
    QString address;       // « %I0.3 » — recalculee, jamais stockee
    QString terminal;      // borne du module, « 03 »
    QString description;   // la seule chose saisie a la main
    QString folio;
    QString zone;
    QString target;        // appareil raccorde a ce point
    QString targetPin;
    QString wireNumber;
};

class Reports
{
public:
    // Regroupe les instances par article. Deux appareils de meme reference et
    // de meme valeur ne font qu'une ligne, avec la liste de leurs designations.
    static QVector<BomLine> billOfMaterials(const Project &project,
                                            const ReportScope &scope = {});

    // Bornier : une ligne par borne, avec le fil et l'appareil raccordes.
    static QVector<TerminalLine> terminalList(const Project &project, const Netlist &netlist,
                                              const ReportScope &scope = {});

    // Liste des fils : un potentiel par ligne, avec ses extremites.
    static QVector<WireLine> wireList(const Project &project, const Netlist &netlist,
                                      const ReportScope &scope = {});

    // Cablage De/Vers : une ligne par liaison a realiser. Un potentiel a n
    // broches donne n-1 liaisons, chainees de proche en proche — c'est ainsi
    // qu'un cableur tire ses fils, et cela rend le rapport directement
    // executable a l'atelier.
    static QVector<WireRunLine> wireFromTo(const Project &project, const Netlist &netlist,
                                           const ReportScope &scope = {});

    // Liste des composants : un appareil par ligne, avec son folio, sa zone
    // et ses donnees catalogue. A ne pas confondre avec la nomenclature, qui
    // regroupe par article a commander.
    static QVector<ComponentLine> componentList(const Project &project, const Netlist &netlist,
                                                const ReportScope &scope = {});

    // Adresses et descriptions d'E/S d'automate : un point par ligne, dans
    // l'ordre des modules puis des points. La base sert a retrouver le format
    // d'adressage du constructeur — sans elle, un module pose reste lisible
    // mais ses adresses ne peuvent plus etre recalculees.
    static QVector<PlcIoLine> plcIoList(const Project &project, const Netlist &netlist,
                                        const PlcDatabase &database,
                                        const ReportScope &scope = {});

    static ReportTable toTable(const QVector<BomLine> &lines);
    static ReportTable toTable(const QVector<TerminalLine> &lines);
    static ReportTable toTable(const QVector<WireLine> &lines);
    static ReportTable toTable(const QVector<WireRunLine> &lines);
    static ReportTable toTable(const QVector<ComponentLine> &lines);
    static ReportTable toTable(const QVector<PlcIoLine> &lines);

    // Recapitulatif du projet, pour le panneau de controle.
    static ReportTable projectSummary(const Project &project, const Netlist &netlist,
                                      const ReportScope &scope = {});
    static ReportTable diagnostics(const Netlist &netlist, const ReportScope &scope = {});
};

} // namespace dsn
