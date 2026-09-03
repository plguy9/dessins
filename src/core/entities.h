// Les six types d'entites d'un folio.
#pragma once

#include "entity.h"
#include "primitive.h"

#include <QMap>
#include <QStringList>
#include <QVector>

namespace dsn {

// --------------------------------------------------------------------------
// Instance de symbole.
//
// L'instance ne porte pas son graphisme : elle reference une definition de la
// bibliotheque. La boite englobante locale est mise en cache lors de la
// resolution contre la bibliotheque, ce qui evite au coeur de dependre du
// module symbols.
class SymbolInstance : public Entity
{
public:
    EntityType type() const override { return EntityType::Symbol; }
    QString typeTag() const override { return QStringLiteral("symbol"); }
    EntityPtr clone() const override;
    bool assign(const Entity &other) override;
    QRectF boundingBox() const override;
    void translate(const QPointF &delta) override;
    void scale(const QPointF &base, double factor) override;
    QJsonObject toJson() const override;
    bool readJson(const QJsonObject &object) override;

    QString definitionId;
    Placement placement;

    // Champs libres : designation, valeur, reference fabricant, fonction,
    // localisation... Leur liste depend du profil metier, pas du coeur.
    QMap<QString, QString> fields;

    // Appareils multi-blocs : une bobine de contacteur et ses contacts
    // auxiliaires partagent un groupe, donc une seule designation.
    QString deviceGroup;
    int blockIndex = 0;

    // Une designation saisie a la main n'est jamais ecrasee par une
    // regeneration automatique.
    bool designationLocked = false;

    QString designation() const { return fields.value(QStringLiteral("designation")); }
    void setDesignation(const QString &d) { fields.insert(QStringLiteral("designation"), d); }

    const QRectF &localBounds() const noexcept { return m_localBounds; }
    void setLocalBounds(const QRectF &r) { m_localBounds = r; }

private:
    QRectF m_localBounds{ -2.5, -2.5, 5.0, 5.0 };
};

// --------------------------------------------------------------------------
// Fil.
//
// Une liaison porte n conducteurs des l'origine, avec n = 1 comme cas courant.
// C'est la decision qui rend l'unifilaire (ou un trait unique represente
// L1/L2/L3 + N + PE) realisable sans reecrire le coeur.
class Wire : public Entity
{
public:
    EntityType type() const override { return EntityType::Wire; }
    QString typeTag() const override { return QStringLiteral("wire"); }
    EntityPtr clone() const override;
    bool assign(const Entity &other) override;
    QRectF boundingBox() const override;
    void translate(const QPointF &delta) override;
    void scale(const QPointF &base, double factor) override;
    QJsonObject toJson() const override;
    bool readJson(const QJsonObject &object) override;

    QVector<QPointF> points;

    // Vide : un conducteur anonyme. Sinon un nom par conducteur (L1, L2, N, PE).
    QStringList conductors;

    QString number;             // repere du fil
    bool numberLocked = false;  // repere saisi a la main
    QString style;              // trait, tirets, mixte... resolu par le profil

    // Identifiant du type de fil (voir WireType). Vide = type par defaut :
    // un fil ancien, ou pose sans choix explicite, reste tracable.
    QString wireType;

    // LE CABLE AUQUEL CE CONDUCTEUR APPARTIENT — son nom, pas son type.
    //
    // « 2PR#16CU » dit ce qu'on commande ; « 022TT8917A » dit LEQUEL, dans
    // cette armoire-la. Le premier est le type de fil, le second est ici.
    //
    // Le cable n'est pas une entite : il n'a pas de trace propre, c'est un
    // groupe de conducteurs qui vont du meme point au meme point. Lui donner
    // une entite obligerait a la tenir synchronisee avec ses fils — et un
    // cable qui ne suit pas ses conducteurs est faux des le premier
    // deplacement.
    QString cable;

    int conductorCount() const { return conductors.isEmpty() ? 1 : int(conductors.size()); }
    QString conductorName(int index) const;

    QPointF start() const { return points.isEmpty() ? QPointF() : points.first(); }
    QPointF end() const { return points.isEmpty() ? QPointF() : points.last(); }
    double length() const;
    bool isDegenerate() const;
};

// --------------------------------------------------------------------------
// Point de jonction explicite (le point de connexion dessine).
class Junction : public Entity
{
public:
    EntityType type() const override { return EntityType::Junction; }
    QString typeTag() const override { return QStringLiteral("junction"); }
    EntityPtr clone() const override;
    bool assign(const Entity &other) override;
    QRectF boundingBox() const override;
    void translate(const QPointF &delta) override;
    void scale(const QPointF &base, double factor) override;
    QJsonObject toJson() const override;
    bool readJson(const QJsonObject &object) override;

    QPointF point;
    double diameter = 1.2;
};

// --------------------------------------------------------------------------
// Texte libre.
class TextItem : public Entity
{
public:
    enum class Align { Left, Center, Right };

    EntityType type() const override { return EntityType::Text; }
    QString typeTag() const override { return QStringLiteral("text"); }
    EntityPtr clone() const override;
    bool assign(const Entity &other) override;
    QRectF boundingBox() const override;
    void translate(const QPointF &delta) override;
    void scale(const QPointF &base, double factor) override;
    QJsonObject toJson() const override;
    bool readJson(const QJsonObject &object) override;

    QString text;
    Placement placement;
    double height = 2.5; // hauteur de capitale en mm, comme sur un plan cote
    Align align = Align::Left;
};

// --------------------------------------------------------------------------
// Primitive graphique d'annotation (cadres, reperages, fonds de plan).
//
// L'entite n'est qu'une enveloppe autour de Primitive : le graphisme des
// symboles utilise le meme type, donc le meme code de rendu.
class GraphicItem : public Entity
{
public:
    EntityType type() const override { return EntityType::Graphic; }
    QString typeTag() const override { return QStringLiteral("graphic"); }
    EntityPtr clone() const override;
    bool assign(const Entity &other) override;
    QRectF boundingBox() const override;
    void translate(const QPointF &delta) override;
    void scale(const QPointF &base, double factor) override;
    QJsonObject toJson() const override;
    bool readJson(const QJsonObject &object) override;

    Primitive shape;
};

// --------------------------------------------------------------------------
// Etiquette de potentiel et renvoi de folio.
//
// Les deux servent la meme fonction electrique : forcer la fusion de plusieurs
// groupes de connexite portant le meme nom. Ils ne different que par leur
// portee et par ce qu'ils affichent.
class Label : public Entity
{
public:
    enum class Scope {
        Folio,  // etiquette locale : ne fusionne que dans son folio
        Project // renvoi de folio : fusionne a travers tout le projet
    };

    // Role de renvoi, repris des fleches de signal d'AutoCAD Electrical.
    // Une source marque l'origine d'un signal qui se poursuit ailleurs ; une
    // destination marque sa reprise. Les deux portent le meme nom de code et
    // sont alors le meme potentiel. Un renvoi simple ne dit pas dans quel
    // sens va le signal — c'est utile pour un potentiel d'alimentation, qui
    // n'a pas de sens de lecture.
    enum class Role { Plain, Source, Destination };

    EntityType type() const override { return EntityType::Label; }
    QString typeTag() const override { return QStringLiteral("label"); }
    EntityPtr clone() const override;
    bool assign(const Entity &other) override;
    QRectF boundingBox() const override;
    void translate(const QPointF &delta) override;
    void scale(const QPointF &base, double factor) override;
    QJsonObject toJson() const override;
    bool readJson(const QJsonObject &object) override;

    QPointF point;
    QString name;
    Direction direction = Direction::Right;
    Scope scope = Scope::Folio;
    Role role = Role::Plain;
    double height = 2.0;

    // Une fleche de signal est par definition inter-folios : lui donner une
    // portee locale la rendrait muette.
    bool isSignalArrow() const { return role != Role::Plain; }
};

// --------------------------------------------------------------------------
// Cotation lineaire.
//
// Elle manquait, et ce n'etait pas une case de ruban : un schema d'armoire
// porte des cotes — l'entraxe de deux rails, la hauteur d'un jeu de barres,
// l'encombrement d'un coffret. Sans elles, on ecrit « 150 » a cote d'un trait
// et plus rien ne garantit que le trait mesure 150.
//
// Trois decisions gouvernent ce type :
//
// 1. **La cote est MESUREE, jamais saisie.** La valeur se deduit des deux
//    points d'attache. Deplacer une attache change le nombre ; c'est toute la
//    difference avec un texte pose a cote, et c'est ce qui empeche un plan de
//    mentir. `override` existe pour le cas rare (rupture d'echelle) et se voit
//    comme ce qu'il est : une valeur imposee a la main.
// 2. **La geometrie se calcule en UN endroit** (`geometry()`), dans le coeur.
//    Le peintre et l'export DXF la lisent tous les deux ; la recalculer de
//    chaque cote les ferait diverger d'un demi-millimetre, ce qui se voit sur
//    une fleche.
// 3. **Le decalage est donne par un point**, pas par une distance signee. Le
//    troisieme clic pose la ligne de cote la ou on la veut, des deux cotes de
//    la mesure, sans avoir a penser au signe.
class DimensionItem : public Entity
{
public:
    // Alignee suit la direction des deux points ; horizontale et verticale ne
    // mesurent qu'une projection. Les trois existent parce qu'un schema est
    // fait de traits droits : coter l'entraxe horizontal de deux rails ne doit
    // pas dependre du fait qu'on a designe deux points exactement a la meme
    // hauteur.
    enum class Kind { Aligned, Horizontal, Vertical };

    // Ce que le peintre et l'export ont besoin de savoir, calcule une fois.
    struct Geometry {
        QPointF lineStart;   // ligne de cote
        QPointF lineEnd;
        QPointF firstFrom;   // ligne d'attache du premier point
        QPointF firstTo;
        QPointF secondFrom;  // ligne d'attache du second
        QPointF secondTo;
        QPointF textAt;      // milieu de la ligne de cote
        double angleDegrees = 0.0; // orientation du texte, dans le sens de la cote
        double value = 0.0;        // la mesure, en millimetres
    };

    EntityType type() const override { return EntityType::Dimension; }
    QString typeTag() const override { return QStringLiteral("dimension"); }
    EntityPtr clone() const override;
    bool assign(const Entity &other) override;
    QRectF boundingBox() const override;
    void translate(const QPointF &delta) override;
    void scale(const QPointF &base, double factor) override;
    QJsonObject toJson() const override;
    bool readJson(const QJsonObject &object) override;

    QPointF first;      // premier point d'attache
    QPointF second;     // second point d'attache
    QPointF linePoint;  // un point par lequel passe la ligne de cote
    Kind kind = Kind::Aligned;
    double textHeight = 2.5;   // serie ISO 3098, comme les textes
    int decimals = 0;          // un schema se cote au millimetre entier
    QString suffix;            // vide = pas d'unite ecrite
    QString override;          // valeur imposee a la main, vide = la mesure

    double measure() const;
    QString displayText() const;
    Geometry geometry() const;

    static QString kindTag(Kind kind);
    static Kind kindFromTag(const QString &tag);
};

} // namespace dsn
