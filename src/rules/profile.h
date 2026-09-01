// Profil metier.
//
// Un profil est ce qui fait la difference entre les trois metiers vises :
// meme moteur, meme modele de document, mais une norme de symboles, des
// regles de reperage, un pas de grille et des formats de feuille differents.
// Ajouter un metier est ici un travail de donnees, pas de code.
#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace dsn {

// Attribution des reperes de fil.
struct WireNumberingRule {
    enum class Strategy {
        Sequential,   // 1, 2, 3... sur tout le projet ou par folio
        FolioColumn,  // folio + colonne du cadre, ex. 305 pour folio 3 colonne 5
        PotentialName // le nom de l'etiquette quand il existe, sequentiel sinon
    };

    Strategy strategy = Strategy::FolioColumn;
    QString prefix;
    int start = 1;
    int step = 1;
    int padding = 0;      // remplissage a gauche par des zeros
    bool perFolio = true; // remise a zero du compteur a chaque folio

    QString format(int value) const;
    static QString strategyTag(Strategy s);
    static Strategy strategyFromTag(const QString &tag);
};

// Ce que le format de repere a sa disposition pour un appareil donne.
struct DesignationContext {
    QString family;          // %F — prefixe de famille : K, Q, X...
    int number = 1;          // %N — compteur sequentiel
    QString suffix;          // lettre de departage, en mode reference de ligne
    QString sheet;           // %S — numero de folio
    QString lineReference;   // %X — reference de ligne, ex. 104
    QString installation;    // %I — code d'installation
    QString location;        // %L — code d'emplacement
};

// Attribution des designations d'appareil.
struct DesignationRule {
    // Les deux modes d'AutoCAD Electrical. En sequentiel, le numero vient
    // d'un compteur par famille : KM1, KM2. Base sur la reference, il vient
    // de l'endroit ou l'appareil est pose : 104K pour un appareil en colonne
    // 4 du folio 1. Le second se lit sur le schema sans chercher la
    // nomenclature — c'est pour cela qu'il existe.
    enum class Mode { Sequential, LineReference };

    // La CEI 81346 prefixe d'un tiret : -K1, -Q2. L'usage nord-americain non.
    bool leadingDash = true;
    bool perFolio = false;
    int padding = 0;
    Mode mode = Mode::Sequential;

    // Format a parametres remplacables, comme le « Component TAG Format »
    // d'AutoCAD. Vide = le format par defaut du mode. Jetons reconnus :
    // %F famille, %N numero, %S folio, %X reference de ligne,
    // %I installation, %L emplacement, %% un pour cent litteral.
    QString tagFormat;

    // Prefixe impose par famille d'appareil, prioritaire sur celui du symbole.
    QMap<QString, QString> prefixByDeviceKind;

    // Format effectif : celui qui est regle, ou celui du mode.
    QString effectiveFormat() const;

    QString format(const DesignationContext &context) const;
    // Confort : repere sequentiel simple, sans contexte de position.
    QString format(const QString &prefix, int index) const;

    static QString modeTag(Mode m);
    static Mode modeFromTag(const QString &tag);
};

class Profile
{
public:
    QString id;
    QString name;
    QString norm;        // norme de symboles : IEC ou ANSI
    QString unitSystem;  // metric ou imperial
    double gridStep = 2.5;
    QString defaultSheetFormat = QStringLiteral("A3");

    WireNumberingRule wireNumbering;
    DesignationRule designation;

    // Conducteurs proposes par defaut pour une liaison multiple.
    QStringList defaultConductors;

    bool isImperial() const { return unitSystem == QLatin1String("imperial"); }

    static Profile iec();
    static Profile ansi();
    static Profile electronic();
    static QList<Profile> all();
    static Profile byId(const QString &id); // repli sur le profil CEI
};

} // namespace dsn
