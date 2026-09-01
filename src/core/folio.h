// Un folio est une page du dossier : un format de feuille, un cadre decoupe en
// zones, un cartouche et une liste d'entites.
#pragma once

#include "entity.h"

#include <QMap>
#include <QString>

#include <vector>

namespace dsn {

// Decoupage du cadre en zones de reperage (colonnes numerotees, lignes
// lettrees). Le reperage des fils et les renvois de folio s'y adossent.
struct SheetFrame {
    double margin = 10.0;          // marge exterieure du cadre
    double bindingMargin = 20.0;   // marge de reliure a gauche
    int columns = 10;
    int rows = 6;
    bool showZoneLabels = true;
    double titleBlockWidth = 180.0;
    double titleBlockHeight = 40.0;

    QJsonObject toJson() const;
    static SheetFrame fromJson(const QJsonValue &v);
};

class Folio
{
public:
    Folio();
    explicit Folio(QString id);
    Folio(const Folio &other);
    Folio &operator=(const Folio &other);
    Folio(Folio &&) noexcept = default;
    Folio &operator=(Folio &&) noexcept = default;
    ~Folio();

    const QString &id() const noexcept { return m_id; }
    void setId(QString id) { m_id = std::move(id); }

    QString number;                    // repere affiche du folio
    QString title;
    SheetFormat sheet = sheetFormatById(QStringLiteral("A3"));
    SheetFrame frame;
    QMap<QString, QString> titleBlock; // champs propres au folio

    // ---- entites -------------------------------------------------------
    const std::vector<EntityPtr> &entities() const noexcept { return m_entities; }
    std::vector<EntityPtr> &entities() noexcept { return m_entities; }
    std::size_t entityCount() const noexcept { return m_entities.size(); }

    Entity *addEntity(EntityPtr entity);
    EntityPtr takeEntity(const QString &entityId);
    bool removeEntity(const QString &entityId);
    // Remplace une entite en conservant sa position dans l'ordre de trace.
    bool replaceEntity(EntityPtr entity);
    int indexOfEntity(const QString &entityId) const;
    Entity *insertEntity(int index, EntityPtr entity);
    Entity *entity(const QString &entityId);
    const Entity *entity(const QString &entityId) const;
    void clearEntities();

    template <typename T>
    std::vector<T *> entitiesOfType()
    {
        std::vector<T *> out;
        for (const EntityPtr &e : m_entities) {
            if (auto *typed = dynamic_cast<T *>(e.get()))
                out.push_back(typed);
        }
        return out;
    }

    template <typename T>
    std::vector<const T *> entitiesOfType() const
    {
        std::vector<const T *> out;
        for (const EntityPtr &e : m_entities) {
            if (const auto *typed = dynamic_cast<const T *>(e.get()))
                out.push_back(typed);
        }
        return out;
    }

    // ---- geometrie de la feuille ---------------------------------------
    QRectF sheetRect() const;   // la feuille entiere
    QRectF frameRect() const;   // l'interieur du cadre
    QRectF titleBlockRect() const;
    QRectF zoneRect(int column, int row) const;

    // Repere de zone d'un point, ex. "B4". Vide si le point est hors cadre.
    QString zoneAt(const QPointF &p) const;
    int columnAt(const QPointF &p) const; // 1..columns, ou -1

    QRectF contentBounds() const;

    QJsonObject toJson() const;
    bool readJson(const QJsonObject &object);

private:
    QString m_id;
    std::vector<EntityPtr> m_entities;
};

} // namespace dsn
