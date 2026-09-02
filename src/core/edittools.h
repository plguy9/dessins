// Les commandes de modification 2D d'AutoCAD, cote calcul.
//
// Comme wiretools et componenttools, ce fichier ne connait ni commande ni
// interface : il rend des resultats, et c'est la couche au-dessus qui en fait
// des operations annulables. C'est ce qui permet de tester RESEAU et ALIGNER
// sans ouvrir de fenetre.
//
// Trois familles y vivent :
//
//   RESEAU (ARRAY)   — la matrice de copies, rectangulaire ou polaire. Sur un
//                      schema, huit departs moteur identiques se posent en un
//                      geste au lieu de huit copier-coller alignes a la main.
//   ALIGNER          — mettre une selection au cordeau, ou la repartir a pas
//                      egal. AutoCAD ne l'a pas en standard ; tous les
//                      logiciels de dessin modernes si, et c'est ce qui
//                      distingue un folio propre d'un folio a peu pres droit.
//   JOINDRE / COUPER — souder deux fils colineaires en un seul, ou couper un
//                      fil en deux. Le pendant d'AJUSTER et PROLONGER.
#pragma once

#include "entities.h"
#include "folio.h"

#include <QPointF>
#include <QVector>

#include <optional>

namespace dsn {

// --------------------------------------------------------------------------
// RESEAU

struct ArraySpec {
    enum class Kind { Rectangular, Polar };

    Kind kind = Kind::Rectangular;

    // Rectangulaire : nombre de colonnes et de lignes, pas entre elles. Un
    // pas negatif construit le reseau vers la gauche ou vers le haut, ce qui
    // evite d'avoir a dessiner le motif du bon cote.
    int columns = 2;
    int rows = 1;
    double columnSpacing = 20.0;
    double rowSpacing = 20.0;

    // Polaire : centre, nombre d'elements et angle total balaye. Les elements
    // pivotent avec le rayon, sauf a demander le contraire — un appareil
    // couche sur le flanc n'est plus lisible.
    QPointF center;
    int count = 4;
    double totalAngle = 360.0;
    bool rotateItems = true;

    // Nombre total de copies produites, l'original compris.
    int itemCount() const;
    bool isValid() const;
};

// La transformation d'une copie du reseau. `angle` est nul pour un reseau
// rectangulaire : c'est le cas courant, et un schema ne pivote pas ses
// appareils sans raison.
struct ArrayPlacement {
    QPointF offset;    // deplacement du point de base vers la copie
    double angle = 0.0; // rotation autour du centre, en degres
    int column = 0;
    int row = 0;
    int index = 0;     // rang dans le reseau, 0 pour l'original
};

class ArrayTools
{
public:
    // Les placements du reseau, l'original inclus en premier. `anchor` est le
    // point de reference du motif : son barycentre pour un reseau polaire,
    // sans importance pour un rectangulaire.
    static QVector<ArrayPlacement> placements(const ArraySpec &spec, const QPointF &anchor);

    // Applique un placement a une copie deja clonee. La rotation d'un reseau
    // polaire est arrondie au quart de tour pour les symboles, parce que le
    // modele n'accepte que ceux-la — et c'est voulu (voir Orientation).
    static void apply(Entity &entity, const ArrayPlacement &placement, const QPointF &center);
};

// --------------------------------------------------------------------------
// ALIGNER et REPARTIR

enum class AlignMode {
    Left,
    HorizontalCenter,
    Right,
    Top,
    VerticalCenter,
    Bottom,
    DistributeHorizontally,
    DistributeVertically
};

QString alignModeLabel(AlignMode mode);

class AlignTools
{
public:
    // Le deplacement a appliquer a chaque entite, dans l'ordre recu. Vide
    // quand l'alignement n'a pas de sens : aligner une seule entite ne veut
    // rien dire, en repartir deux non plus.
    static QVector<QPointF> offsets(const QVector<QRectF> &boxes, AlignMode mode);

    // Nombre minimal d'entites pour que le mode ait un sens.
    static int minimumCount(AlignMode mode);
};

// --------------------------------------------------------------------------
// JOINDRE et COUPER

struct WireJoin {
    QString firstId;
    QString secondId;
    QVector<QPointF> merged;   // le trace du fil resultant
};

struct WireCut {
    QString wireId;
    QVector<QPointF> before;
    QVector<QPointF> after;
};

class EditTools
{
public:
    // Deux fils qui se touchent par un bout et dont les segments joints sont
    // colineaires n'en font qu'un. On refuse de souder deux fils de types
    // differents : le resultat porterait une couleur, et l'autre serait perdue
    // sans que personne ne le voie.
    static std::optional<WireJoin> joinable(const Folio &folio, const QString &firstId,
                                            const QString &secondId);

    // Coupe un fil au point donne. Le point doit tomber sur le trace et pas
    // sur une extremite : couper au bout ne produirait qu'un fil vide.
    static std::optional<WireCut> cut(const Folio &folio, const QString &wireId,
                                      const QPointF &at);
};

} // namespace dsn
