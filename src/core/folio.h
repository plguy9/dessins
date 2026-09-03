// Un folio est une page du dossier : un format de feuille, un cadre decoupe en
// zones, un cartouche et une liste d'entites.
#pragma once

#include "entity.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

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

    // LE SENS DU REPERAGE, et ce n'est pas un detail d'affichage.
    //
    // Notre convention : 1 a gauche, A en haut. Celle des planches relevees
    // (docs/BOUCLES.md) : la zone A est a DROITE, du cote du cartouche, et le
    // 1 est EN BAS. Deux bureaux d'etudes a seize ans d'ecart la partagent —
    // c'est une convention de metier, pas une lubie.
    //
    // Tous les renvois du dossier en dependent : une planche reproduite avec
    // l'autre sens renvoie vers la mauvaise case, et rien ne le signale.
    bool columnsRightToLeft = false;
    bool rowsBottomToTop = false;

    QJsonObject toJson() const;
    static SheetFrame fromJson(const QJsonValue &v);
};

// UNE BANDE DE LOCALISATION.
//
// Sur un schema de boucle, la feuille est coupee sur toute sa hauteur en
// bandes verticales nommees — « CHAMP », « CABINET 037BJ0151 » — chacune avec
// son bandeau d'en-tete. La bande est a un schema de boucle ce que l'echelle
// de commande est a un schema de commande : la structure qui organise la page.
//
// Et elle porte un SENS : c'est la localisation de tout ce qu'elle contient.
// Un capteur est *au champ*, une borne est *dans l'armoire* — c'est exactement
// ce qu'un rapport de cablage imprime dans ses colonnes « de » et « vers ».
// L'appartenance se DEDUIT de l'abscisse, comme `Folio::zoneAt()` deduit la
// zone : rien a stocker sur l'entite, donc rien qui puisse se desynchroniser
// quand on la deplace.
struct FolioBand {
    QString title;
    double width = 60.0;   // en millimetres, depuis la bande precedente

    QJsonObject toJson() const;
    static FolioBand fromJson(const QJsonValue &v);
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

    // LES TABLES DU CARTOUCHE, par clef : « revisions », « references »,
    // « routing »… et n'importe quelle table qu'un gabarit maison declare.
    // Une table est une clef et des lignes de texte ; c'est le gabarit qui
    // dit ses colonnes. Ce choix rend une table maison gratuite : ajouter
    // « ESSAIS EN USINE » a son cartouche ne demande pas une ligne de code.
    QMap<QString, QVector<QStringList>> tables;

    // Les bandes de localisation, de gauche a droite. Vides = pas de bandes,
    // et le folio se dessine comme avant. La DERNIERE s'etire jusqu'au bord
    // droit du cadre quelle que soit sa largeur declaree : sans cela un
    // changement de format laisserait une lisiere sans nom, et une entite
    // posee dedans n'aurait pas de localisation.
    QVector<FolioBand> bands;
    double bandHeaderHeight = 6.0;   // hauteur du bandeau de titre

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

    // Le nom de la bande qui contient ce point, ou une chaine vide. C'est la
    // LOCALISATION de ce qui s'y trouve : le rapport de cablage la lit pour
    // remplir ses colonnes « de » et « vers ».
    QString bandAt(const QPointF &p) const;
    int bandIndexAt(const QPointF &p) const;  // -1 si hors bande
    QRectF bandRect(int index) const;         // la bande, bandeau compris

    QRectF contentBounds() const;

    QJsonObject toJson() const;
    bool readJson(const QJsonObject &object);

private:
    QString m_id;
    std::vector<EntityPtr> m_entities;
};

} // namespace dsn
