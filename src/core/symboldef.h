// Definition d'un symbole : son graphisme et, surtout, ses broches.
//
// La broche est ce qui distingue un logiciel electrique d'un logiciel de
// dessin : elle porte une position, un sens, un repere et un type electrique.
// C'est sur elle que s'appuie toute l'extraction de connectivite.
#pragma once

#include "primitive.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dsn {

enum class PinType {
    Passive,       // borne sans polarite : le cas courant en electrotechnique
    Input,
    Output,
    Bidirectional,
    Power,         // arrivee de puissance
    Ground,        // terre / masse
    Terminal,      // borne de bornier
    NotConnected
};

QString pinTypeTag(PinType t);
PinType pinTypeFromTag(const QString &tag);

struct Pin {
    QString number;    // repere affiche : 1, 2, A1, A2, 13, 14
    QString name;      // nom fonctionnel, souvent masque
    QPointF position;  // extremite libre, en coordonnees locales du symbole
    Direction direction = Direction::Right; // sens sortant du corps
    double length = 2.5;
    PinType type = PinType::Passive;
    bool showName = false;
    bool showNumber = true;

    // Point d'attache au corps du symbole : la broche est tracee de la racine
    // vers la position, cette derniere etant le point electriquement connecte.
    QPointF root() const { return position - unitVector(direction) * length; }

    QJsonObject toJson() const;
    static Pin fromJson(const QJsonValue &v);
};

class SymbolDefinition
{
public:
    // Identifiant complet, ex. "iec:contactor-coil". Compose de la norme et de
    // l'identifiant logique, ce qui permet de basculer un projet d'un jeu de
    // symboles a l'autre sans toucher aux instances.
    QString id;
    QString logicalId;
    QString norm = QStringLiteral("IEC");

    QString name;
    QString category;
    QStringList keywords;

    // Prefixe de designation, resolu par le profil metier (K, Q, F en CEI ;
    // K, CB, FU en ANSI ; R, C en electronique).
    QString designationPrefix;

    // Famille d'appareil, utilisee pour regrouper les blocs d'un meme appareil
    // (une bobine et ses contacts auxiliaires) et pour les rapports.
    QString deviceKind;

    QVector<Primitive> graphics;
    QVector<Pin> pins;

    // CE SYMBOLE NE SE RACCORDE PAS, et c'est voulu.
    //
    // Un symbole sans broche est normalement une faute — c'est un appareil
    // qu'on ne pourra jamais cabler, et rien ne le dirait avant l'usage. Mais
    // quelques-uns n'en portent legitimement aucune : une boite de jonction
    // est une enveloppe, une etiquette de cable est une annotation, un
    // serpentin est un element de procede. Leur donner une broche factice
    // serait pire : elles entreraient dans la netlist et dans le rapport de
    // cablage, et un fil pose dessus se ferait couper.
    //
    // Le declarer vaut mieux que de relacher le controle : le test exige
    // toujours des broches, mais il accepte celles qui disent ne pas en
    // vouloir.
    bool noConnections = false;
    QMap<QString, QString> defaultFields;

    // Emplacements des textes attaches (designation, valeur), en local.
    QPointF designationAnchor{ 0.0, -6.0 };
    QPointF valueAnchor{ 0.0, 6.0 };

    QRectF bodyBounds() const;   // graphisme seul
    QRectF bounds() const;       // graphisme + broches
    const Pin *pin(const QString &number) const;
    bool isValid() const { return !id.isEmpty(); }

    QJsonObject toJson() const;
    static SymbolDefinition fromJson(const QJsonValue &v);

    static QString makeId(const QString &norm, const QString &logicalId);
};

} // namespace dsn
