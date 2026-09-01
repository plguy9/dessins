// Catalogue fabricant — l'equivalent du default_cat.mdb d'AutoCAD Electrical.
//
// Un schema sans references fabricant ne se commande pas. Le catalogue relie
// une famille d'appareil (contacteur, disjoncteur, borne) a des articles
// reels, et la boite du composant y puise au lieu de faire retaper une
// reference a la main sur chaque appareil.
//
// La source est un JSON, embarque par ressource comme la bibliotheque de
// symboles, et complete par les fichiers de l'utilisateur : un bureau
// d'etudes a son catalogue, pas celui du logiciel.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace dsn {

struct CatalogItem {
    QString deviceKind;    // famille visee : contactor, breaker, terminal...
    QString manufacturer;
    QString partNumber;
    QString description;
    QString rating;        // calibre : 9 A, 16 A, 2,5 mm²...
    QString voltage;
    QString note;

    bool isValid() const { return !partNumber.isEmpty(); }
    // Un texte unique pour la recherche libre, comme le champ de la boite.
    QString searchText() const;

    QJsonObject toJson() const;
    static CatalogItem fromJson(const QJsonValue &value);
};

class Catalog
{
public:
    void insert(const CatalogItem &item);
    void clear() { m_items.clear(); }
    int count() const { return int(m_items.size()); }
    bool isEmpty() const { return m_items.isEmpty(); }

    const QList<CatalogItem> &items() const { return m_items; }

    // Articles d'une famille. Une famille vide rend tout le catalogue : la
    // boite doit pouvoir chercher hors de la famille du symbole, parce qu'un
    // catalogue reel ne colle jamais parfaitement a nos familles.
    QList<CatalogItem> forDeviceKind(const QString &deviceKind) const;

    // Recherche libre, insensible a la casse et aux accents absents.
    QList<CatalogItem> search(const QString &text, const QString &deviceKind = QString()) const;

    QStringList deviceKinds() const;

    bool readJson(const QByteArray &json, QString *error = nullptr);
    QByteArray toJson() const;

    // Catalogue livre avec le logiciel, charge depuis la ressource Qt, puis
    // complete par les fichiers .json des dossiers de l'utilisateur.
    static Catalog builtin();
    static QStringList userSearchPaths();
    static Catalog loadAll();

private:
    QList<CatalogItem> m_items;
};

} // namespace dsn
