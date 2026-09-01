// Saisie de coordonnees au clavier, a la maniere d'AutoCAD.
//
// C'est ce qui separe dessiner de pointer : on vise une direction a la souris
// et on tape la cote. Sans cela, toute mesure exacte passe par la grille, et
// un schema se dessine au jugé.
//
// Formes acceptees, reprises d'AutoCAD :
//   50          distance directe — 50 mm dans la direction visee
//   50<45       polaire — 50 mm a 45 degres du point de depart
//   @10,5       relatif — 10 a droite et 5 vers le bas du point de depart
//   10,5        relatif aussi : c'est le defaut de la saisie dynamique
//   #10,5       absolu — coordonnees dans le folio
//
// Le separateur de coordonnees est la virgule, comme partout en CAO ; le
// separateur decimal est donc le point. Le point-virgule est accepte comme
// separateur pour qui prefere ecrire « 50,5 » en decimal.
#pragma once

#include <QPointF>
#include <QString>

#include <optional>

namespace dsn {

struct ParsedCoordinate {
    enum class Kind {
        Distance,  // une longueur seule, dans la direction visee
        Polar,     // longueur et angle depuis le point de depart
        Relative,  // deplacement depuis le point de depart
        Absolute,  // position dans le folio
    };

    Kind kind = Kind::Distance;
    double first = 0.0;   // distance, longueur polaire, dx ou x
    double second = 0.0;  // angle en degres, dy ou y

    // Une saisie polaire ou relative n'a de sens qu'a partir d'un point.
    bool needsOrigin() const { return kind != Kind::Absolute; }
};

class CoordinateEntry
{
public:
    // Analyse le texte saisi. Rien si la forme n'est pas reconnue : mieux
    // vaut ne rien faire qu'interpreter de travers une cote.
    static std::optional<ParsedCoordinate> parse(const QString &text);

    // Point designe par la saisie. `from` est le point de depart du geste,
    // `aim` la direction visee a la souris — c'est elle qui donne son sens a
    // une distance seule. Rien si la saisie exige une origine qui manque.
    static std::optional<QPointF> resolve(const ParsedCoordinate &parsed, const QPointF *from,
                                          const QPointF &aim);

    // Les deux d'un coup.
    static std::optional<QPointF> resolve(const QString &text, const QPointF *from,
                                          const QPointF &aim);

    // Vrai si ce caractere peut commencer une saisie. C'est ce qui decide si
    // une frappe ouvre le champ ou reste un raccourci de commande.
    static bool startsEntry(const QString &text);

    // Angle ecran d'une direction, en degres, sens trigonometrique visuel et
    // origine a trois heures — la convention que lit un dessinateur, alors
    // que l'axe des ordonnees descend a l'ecran.
    static double screenAngle(const QPointF &delta);
    static QPointF directionOfAngle(double degrees);
};

} // namespace dsn
