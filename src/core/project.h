// Le projet : les metadonnees du dossier, le profil metier retenu, la
// bibliotheque de symboles embarquee et la suite ordonnee des folios.
#pragma once

#include "folio.h"
#include "symbollibrary.h"
#include "titleblock.h"
#include "wiretype.h"

#include <QDate>
#include <QMap>
#include <QString>

#include <memory>
#include <vector>

namespace dsn {

struct ProjectInfo {
    QString title;
    QString reference;
    QString client;
    QString author;
    QString revision = QStringLiteral("A");
    QDate date = QDate::currentDate();
    QString notes;
    QMap<QString, QString> extra;

    QJsonObject toJson() const;
    static ProjectInfo fromJson(const QJsonValue &v);
};

class Project
{
public:
    Project();
    ~Project();
    Project(const Project &other);
    Project &operator=(const Project &other);
    Project(Project &&) noexcept = default;
    Project &operator=(Project &&) noexcept = default;

    ProjectInfo info;

    // Identifiant du profil metier : "iec", "ansi", "electronic".
    QString profileId = QStringLiteral("iec");

    // Reglages de reperage propres au projet. Ils priment sur ceux du profil :
    // le format de repere est une convention de bureau d'etudes, pas une
    // norme, et AutoCAD le range de meme dans les proprietes du dessin.
    // Stockes en texte parce que leur interpretation appartient a rules/ et
    // que le coeur n'en depend pas.
    QString designationFormat;   // ex. « %F%N » ; vide = celui du profil
    QString designationMode;     // « sequential » ou « lineReference » ; vide = profil

    // Types de fils du projet (couleur, section, calque). Ils voyagent avec
    // le document : un dossier rouvert ailleurs garde ses couleurs. Un projet
    // neuf part du jeu de sa norme, pas d'une liste vide.
    WireTypeSet wireTypes = WireTypeSet::forNorm(QStringLiteral("iec"));

    // LE CARTOUCHE DU DOSSIER.
    //
    // Il vit ici et voyage dans le fichier, comme la bibliotheque et les types
    // de fils : un dossier rouvert ailleurs garde SON cartouche, meme si le
    // poste ne connait pas le gabarit du bureau qui l'a tire. Vide = le
    // gabarit standard, pour qu'un ancien fichier se tire comme avant.
    TitleBlockTemplate titleBlock;

    // Les images du cartouche — logo, sceau d'ingenieur — par clef, en PNG.
    // EMBARQUEES, jamais referencees par un chemin : un logo pointe sur le
    // disque disparait des que le fichier change de poste, et personne ne
    // s'en apercoit avant l'impression.
    QMap<QString, QByteArray> images;

    // Definitions embarquees dans le document. Un dossier archive se rouvre
    // ainsi a l'identique, meme si la bibliotheque du poste a change depuis.
    SymbolLibrary library;

    // ---- folios --------------------------------------------------------
    int folioCount() const { return int(m_folios.size()); }
    Folio *folioAt(int index);
    const Folio *folioAt(int index) const;
    Folio *folio(const QString &id);
    const Folio *folio(const QString &id) const;
    int indexOf(const QString &folioId) const;

    Folio *addFolio(const QString &title = QString());
    Folio *insertFolio(int index, std::unique_ptr<Folio> folio);
    std::unique_ptr<Folio> takeFolio(int index);
    bool moveFolio(int from, int to);

    std::vector<Folio *> folios();
    std::vector<const Folio *> folios() const;

    // ---- recherche transversale ---------------------------------------
    Entity *findEntity(const QString &entityId, Folio **owner = nullptr);
    const Entity *findEntity(const QString &entityId, const Folio **owner = nullptr) const;

    // Renumerote les folios en 1, 2, 3... en respectant les numeros verrouilles
    // par l'utilisateur (un numero non numerique est considere comme voulu).
    void renumberFolios();

    // Recalcule la boite englobante en cache de chaque instance de symbole
    // contre la bibliotheque. A appeler apres tout chargement ou changement de
    // bibliotheque : sans cela les instances gardent une boite par defaut.
    int resolveSymbolBounds();

    // Instances dont la definition est absente de la bibliotheque.
    QStringList missingDefinitions() const;

    void clear();
    bool isEmpty() const;

    QJsonObject toJson() const;
    bool readJson(const QJsonObject &object);

    static constexpr int kFormatVersion = 1;

private:
    std::vector<std::unique_ptr<Folio>> m_folios;
};

} // namespace dsn
