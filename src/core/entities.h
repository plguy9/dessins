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

} // namespace dsn
