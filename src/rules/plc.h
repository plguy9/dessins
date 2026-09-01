// Automates programmables : modules d'entrees-sorties et adressage.
//
// C'est la fonction d'AutoCAD Electrical qui n'a pas d'equivalent dans un
// logiciel de dessin ordinaire. On choisit un module dans une base de
// constructeurs, on donne son adresse de depart, et le logiciel pose un
// symbole ou chaque point porte deja son adresse : I:3/00, %I0.0,
// %I0.2.5 selon le constructeur. Les adresses ne se retapent pas, et
// surtout elles ne se decalent pas quand on ajoute un module.
//
// Trois decisions structurent ce fichier.
//
// D'abord, un module pose est un **SymbolInstance ordinaire**, pas un type
// d'entite a part. Sa definition de symbole est engendree a l'insertion et
// rangee dans la bibliotheque du projet — qui voyage dans le fichier. Il se
// deplace, se copie, s'annule et se cable comme le reste, et ni le peintre
// ni la netlist n'apprennent quoi que ce soit.
//
// Ensuite, l'adresse d'un point n'est **jamais stockee** : elle se recalcule
// depuis l'adresse de depart et le rang du point, comme la netlist se
// recalcule depuis la geometrie. Changer l'emplacement d'un module readresse
// donc ses seize points d'un coup, sans risque d'en oublier un.
//
// Enfin, un seul compteur de points. Les constructeurs presentent l'adresse
// autrement — Siemens en octet.bit, Allen-Bradley en emplacement/point,
// Omron en mot.bit — mais ce sont des vues d'un meme rang. %B et %b sont
// derives de %P, ils ne sont pas une seconde numerotation.
#pragma once

#include "core/entities.h"
#include "core/symboldef.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace dsn {

// Un module d'entrees-sorties du catalogue constructeur.
struct PlcModuleDef {
    QString id;             // « siemens:6ES7321-1BH02 »
    QString manufacturer;   // « Siemens »
    QString series;         // « SIMATIC S7-300 »
    QString partNumber;     // « 6ES7 321-1BH02-0AA0 »
    QString description;
    QString ioType;         // entree-tor, sortie-tor, entree-analogique...
    QString voltage;
    int points = 8;

    // Format d'adressage du constructeur. Jetons : %R rack, %S emplacement,
    // %P point, %B octet, %b bit, %% pour un pour-cent litteral. Un chiffre
    // avant la lettre donne la largeur : %2P rend « 03 ».
    QString addressFormat;

    // Nombre de bits par octet d'adressage, 0 quand le constructeur numerote
    // ses points a la file. Siemens groupe par 8, Omron par 16.
    int bitsPerByte = 0;

    bool isValid() const { return !id.isEmpty() && points > 0; }
    bool isOutput() const;
    bool isAnalog() const;

    // Un texte unique pour la recherche libre de la boite.
    QString searchText() const;

    QJsonObject toJson() const;
    static PlcModuleDef fromJson(const QJsonValue &value);
};

// Un point d'entree-sortie d'un module pose.
struct PlcPoint {
    int index = 0;         // rang dans le module, 0..points-1
    QString address;       // « %I0.3 » — calcule, jamais stocke
    QString terminal;      // repere de borne du module, « 04 »
    QString description;   // saisie par l'utilisateur, elle stockee
    QString pinNumber;     // la broche du symbole ou le point se raccorde
};

// La base des modules, embarquee par ressource comme le catalogue fabricant
// et la bibliotheque de symboles : le logiciel doit savoir poser un automate
// des le premier lancement, sans fichier a installer.
class PlcDatabase
{
public:
    void insert(const PlcModuleDef &module);
    void clear() { m_modules.clear(); }
    int count() const { return int(m_modules.size()); }
    bool isEmpty() const { return m_modules.isEmpty(); }

    const QList<PlcModuleDef> &modules() const { return m_modules; }
    const PlcModuleDef *find(const QString &id) const;

    QStringList manufacturers() const;
    QList<PlcModuleDef> forManufacturer(const QString &manufacturer) const;
    QList<PlcModuleDef> search(const QString &text,
                               const QString &manufacturer = QString()) const;

    bool readJson(const QByteArray &json, QString *error = nullptr);

    static PlcDatabase builtin();
    static QStringList userSearchPaths();
    static PlcDatabase loadAll();

private:
    QList<PlcModuleDef> m_modules;
};

// L'adressage. Une seule fonction, mais c'est elle qui fait la difference
// entre un dessin d'automate et un dessin qui sert a cabler.
class PlcAddress
{
public:
    // Rend l'adresse d'un point. `point` est le rang absolu du point dans
    // l'espace d'adressage du module (adresse de depart + rang du point).
    static QString format(const QString &pattern, int rack, int slot, int point,
                          int bitsPerByte = 0);
};

// Les champs qu'un module pose porte, et leur lecture. Ils vivent dans le
// `fields` du SymbolInstance : le coeur n'a rien a apprendre des automates.
class PlcModule
{
public:
    // Clefs de champ. Publiques parce que les rapports et la boite les lisent.
    static QString moduleKey() { return QStringLiteral("plc.module"); }
    static QString rackKey() { return QStringLiteral("plc.rack"); }
    static QString slotKey() { return QStringLiteral("plc.slot"); }
    static QString firstPointKey() { return QStringLiteral("plc.point"); }
    static QString descriptionKey(int index);

    static bool isModule(const SymbolInstance &symbol);
    static QString moduleId(const SymbolInstance &symbol);
    static int rack(const SymbolInstance &symbol);
    static int slot(const SymbolInstance &symbol);
    static int firstPoint(const SymbolInstance &symbol);

    // Ecrit l'identite et l'adresse de depart dans l'instance.
    static void configure(SymbolInstance &symbol, const PlcModuleDef &def, int rack, int slot,
                          int firstPoint);
    static void setDescription(SymbolInstance &symbol, int index, const QString &text);
    static QString description(const SymbolInstance &symbol, int index);

    // Les points du module, adresses. Vide si l'instance n'est pas un module
    // ou si sa definition a disparu de la base.
    static QVector<PlcPoint> points(const SymbolInstance &symbol, const PlcDatabase &database);

    // Le symbole d'un module : une boite avec une broche par point, l'adresse
    // ecrite en regard. Il est engendre plutot que dessine a la main parce
    // qu'il depend du nombre de points et du format d'adressage — un module
    // de 32 points n'est pas le meme dessin qu'un module de 4.
    static SymbolDefinition buildSymbol(const PlcModuleDef &def,
                                        const QVector<PlcPoint> &points);

    // Identifiant de la definition engendree pour un module. Le meme module a
    // la meme definition dans tout le projet : deux cartes 1746-IA16 ne
    // doivent pas engendrer deux dessins.
    static QString symbolId(const PlcModuleDef &def);
};

} // namespace dsn
