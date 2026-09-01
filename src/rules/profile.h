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

// Attribution des designations d'appareil.
struct DesignationRule {
    // La CEI 81346 prefixe d'un tiret : -K1, -Q2. L'usage nord-americain non.
    bool leadingDash = true;
    bool perFolio = false;
    int padding = 0;
    // Prefixe impose par famille d'appareil, prioritaire sur celui du symbole.
    QMap<QString, QString> prefixByDeviceKind;

    QString format(const QString &prefix, int index) const;
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
