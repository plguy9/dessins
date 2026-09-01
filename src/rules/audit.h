// Audit electrique — le controle de coherence du dossier.
//
// AutoCAD Electrical presente le sien en une fenetre a categories, avec le
// nombre d'anomalies par famille et le saut vers le dessin fautif. C'est ce
// qui en fait un outil et non une liste : on ne corrige pas un schema en
// lisant un rapport, on le corrige en allant sur le folio.
//
// Deux regles gouvernent ce fichier.
//
// D'abord, **tout constat porte un lieu**. Un message qui dit « repere en
// double » sans dire lequel ni ou coute plus de temps qu'il n'en fait gagner.
// Chaque constat porte donc le folio, l'entite et la zone du cadre — de quoi
// s'y rendre en un clic.
//
// Ensuite, **rien n'est verifie deux fois**. Les constats que la netlist
// produit deja en la construisant (fil en l'air, fleche de signal orpheline)
// sont repris tels quels plutot que recalcules : deux implementations de la
// meme regle finiraient par se contredire, et c'est le genre de divergence
// qu'on ne remarque qu'apres avoir livre le dossier.
#pragma once

#include "core/netlist.h"
#include "core/project.h"
#include "plc.h"
#include "reports.h"

namespace dsn {

struct AuditFinding {
    enum class Severity { Info, Warning, Error };

    QString category;   // « Fils », « Repères », « Signaux », « Automates »…
    QString code;       // « tag.duplicate » — stable, il sert aux tests
    QString message;
    Severity severity = Severity::Warning;

    QString folioId;
    QString entityId;
    QString folioTag;   // numero ou titre du folio, pour l'affichage
    QString zone;       // repere de zone du cadre, ex. « C3 »

    QString severityLabel() const;
};

class Audit
{
public:
    // Les categories, dans l'ordre ou elles comptent. L'ordre est celui du
    // depannage : un symbole manquant fausse tout le reste, un repere absent
    // empeche de commander, une reference absente seulement de chiffrer.
    static QStringList categories();

    // Passe tous les controles. La base des automates est facultative : sans
    // elle, les cartes posees restent lisibles mais leurs adresses ne peuvent
    // plus etre recalculees, donc leur chevauchement plus etre detecte.
    static QVector<AuditFinding> run(const Project &project, const Netlist &netlist,
                                     const PlcDatabase &plc, const ReportScope &scope = {});

    // Nombre de constats par categorie, pour les onglets de la fenetre.
    static QMap<QString, int> countByCategory(const QVector<AuditFinding> &findings);

    static ReportTable toTable(const QVector<AuditFinding> &findings);
};

} // namespace dsn
