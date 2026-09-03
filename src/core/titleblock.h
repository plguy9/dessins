// Le cartouche, comme structure de donnees.
//
// Il etait dessine en dur dans le peintre : trois bandes, six textes, aucune
// prise. Or le cartouche est ce qu'un bureau d'etudes regarde en premier, et
// c'est le seul endroit du dessin qui porte SON identite — son logo, son
// sceau, ses tables de revisions, ses champs a lui. Un dossier tire avec le
// cartouche d'un autre ne part pas au chantier.
//
// Le relevé de trois planches reelles (docs/BOUCLES.md) a montre ce qu'il
// fallait : des tables qui grandissent (revisions, references, cheminement),
// des images (logo, sceau d'ingenieur), et une quinzaine de champs libres.
//
// Quatre decisions gouvernent ce fichier :
//
// 1. **Le gabarit vit dans le projet et voyage dans le fichier**, comme la
//    bibliotheque de symboles et les types de fils. Un dossier rouvert
//    ailleurs garde son cartouche, meme si le poste ne connait pas le
//    gabarit du bureau qui l'a tire.
// 2. **Une cellule ne connait qu'une CLEF, jamais une donnee.** Elle dit
//    « ecris ici la valeur de `client` » ; c'est `TitleBlock::values()` qui
//    sait ou la prendre. Sans cela, ajouter un champ obligerait a toucher le
//    peintre — et le gabarit ne serait plus modifiable par l'utilisateur.
// 3. **Une clef inconnue ecrit du vide, jamais son nom.** Un cartouche qui
//    affiche « %CLIENT » en toutes lettres est pire qu'un cartouche vide :
//    il part a l'impression sans que personne ne le remarque.
// 4. **Les coordonnees sont en millimetres, dans le repere du cartouche**,
//    origine au coin haut-gauche. Le cartouche s'ancre en bas a droite du
//    cadre ; le gabarit porte sa taille, ce qui en fait la seule source de
//    verite — deux tailles finiraient par diverger.
#pragma once

#include "primitive.h"

#include <QMap>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dsn {

class Project;
class Folio;

// Une case du cartouche.
struct TitleBlockCell {
    enum class Kind {
        Field,  // un libelle grave et la valeur d'une clef
        Text,   // un texte fixe (les titres de pave : « APPROBATION »…)
        Image,  // logo, sceau — la clef designe une image du projet
        Table,  // revisions, references, cheminement, ou toute table maison
    };

    // Ou le libelle se pose par rapport a la valeur. Les planches relevees
    // utilisent les deux : « DESSINÉ: C. LAVOIE » sur une ligne, et un
    // libelle grave au-dessus de la valeur dans les cases hautes.
    enum class Layout { Inline, Stacked };

    Kind kind = Kind::Field;
    QRectF rect;                     // mm, repere du cartouche
    QString label;                   // libelle grave ; vide = pas de libelle
    QString key;                     // clef de valeur, d'image ou de table
    QString text;                    // Kind::Text : le texte a ecrire
    QStringList columns;             // Kind::Table : intitules des colonnes
    QVector<double> widths;          // Kind::Table : largeurs relatives
    double labelHeight = 1.6;        // mm, hauteur de capitale du libelle
    double textHeight = 2.5;         // mm, hauteur de capitale de la valeur
    Layout layout = Layout::Inline;
    Primitive::Align align = Primitive::Align::Left;
    bool border = true;              // filet autour de la case

    QJsonObject toJson() const;
    static TitleBlockCell fromJson(const QJsonValue &value);
};

// Le gabarit complet.
struct TitleBlockTemplate {
    QString id;
    QString name;
    double width = 180.0;
    double height = 40.0;
    QVector<TitleBlockCell> cells;

    bool isEmpty() const { return cells.isEmpty(); }

    QJsonObject toJson() const;
    static TitleBlockTemplate fromJson(const QJsonValue &value);
};

class TitleBlock
{
public:
    // Le gabarit livre : celui que le logiciel dessinait en dur, transpose
    // en cellules. Il sert de defaut et de point de depart a qui veut le
    // sien — on modifie plus volontiers un cartouche existant qu'on n'en
    // dessine un sur une page blanche.
    static TitleBlockTemplate standard();

    // Un second gabarit, calque sur les planches de schema de boucle
    // relevees : trois tables qui grandissent, quatre lignes de description,
    // logo et sceau. Il existe pour prouver que le mecanisme suffit a le
    // decrire — si un gabarit reel ne rentre pas, c'est le mecanisme qui est
    // faux, pas le gabarit.
    static TitleBlockTemplate loopSheet();

    static QVector<TitleBlockTemplate> builtins();

    // TOUTES les valeurs disponibles pour un folio donne, par clef. C'est le
    // seul endroit qui sait ou une valeur se prend ; la cellule n'en sait
    // rien, et c'est ce qui rend le gabarit modifiable sans toucher au code.
    static QMap<QString, QString> values(const Project &project, const Folio &folio);

    // Les clefs proposees a l'editeur de gabarit, avec leur libelle en
    // francais. Une liste ecrite a la main mentirait des qu'on ajoute un
    // champ : elle se deduit de `values()` sur un projet d'exemple.
    static QMap<QString, QString> fieldCaptions();
};

} // namespace dsn
