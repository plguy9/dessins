// Base commune a tout ce qui vit dans un folio. La hierarchie reste volontairement
// plate : six types d'entites suffisent a decrire un schema electrique complet.
#pragma once

#include "geometry.h"

#include <QJsonObject>
#include <QString>

#include <memory>

namespace dsn {

enum class EntityType { Symbol, Wire, Junction, Text, Graphic, Label };

class Entity
{
public:
    Entity();
    explicit Entity(QString id);
    virtual ~Entity();

    Entity(const Entity &) = default;
    Entity &operator=(const Entity &) = default;

    virtual EntityType type() const = 0;
    virtual QString typeTag() const = 0;
    virtual std::unique_ptr<Entity> clone() const = 0;

    // Recopie l'etat d'une autre entite du meme type dans celle-ci, sans
    // changer d'objet. Les commandes d'annulation s'en servent pour restaurer
    // un etat : remplacer l'objet invaliderait tous les pointeurs detenus par
    // les vues et les panneaux, ce qui est une source de plantage silencieux.
    // Renvoie faux si l'autre entite n'est pas du meme type.
    virtual bool assign(const Entity &other) = 0;
    virtual QRectF boundingBox() const = 0;
    virtual void translate(const QPointF &delta) = 0;

    // Homothetie autour d'un centre. Symetrique de translate : chaque type
    // sait ce que grossir veut dire pour lui — un symbole change de facteur
    // de placement, un fil deplace ses sommets, un texte grandit sa hauteur.
    // Mettre cette connaissance ailleurs obligerait a un dynamic_cast par
    // type a chaque appel, et un type neuf serait oublie en silence.
    virtual void scale(const QPointF &base, double factor) = 0;

    virtual QJsonObject toJson() const;
    virtual bool readJson(const QJsonObject &object);

    const QString &id() const noexcept { return m_id; }
    void setId(QString id) { m_id = std::move(id); }

    bool isLocked() const noexcept { return m_locked; }
    void setLocked(bool locked) { m_locked = locked; }

protected:
    QString m_id;
    bool m_locked = false;
};

using EntityPtr = std::unique_ptr<Entity>;

// Fabrique utilisee par le chargement de fichier. Renvoie nullptr sur une
// balise inconnue : un document produit par une version plus recente perd
// l'entite plutot que de refuser de s'ouvrir.
EntityPtr createEntity(const QString &typeTag);

QString newId();

} // namespace dsn
