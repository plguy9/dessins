// Registre en memoire des definitions de symboles.
//
// La bibliotheque vit dans le coeur parce que la connectivite en depend : sans
// les broches d'une definition, aucun potentiel ne peut etre calcule. Le
// chargement depuis le disque, lui, est la responsabilite du module symbols.
#pragma once

#include "symboldef.h"

#include <QHash>
#include <QMap>
#include <QStringList>

namespace dsn {

class SymbolLibrary
{
public:
    void insert(const SymbolDefinition &definition);
    void remove(const QString &id);
    void clear();

    bool contains(const QString &id) const;
    const SymbolDefinition *definition(const QString &id) const;

    // Resout un identifiant logique dans une norme donnee, avec repli sur
    // n'importe quelle norme disponible : un projet ANSI reste ouvrable meme
    // si un symbole n'existe pour l'instant qu'en CEI.
    const SymbolDefinition *resolve(const QString &logicalId, const QString &norm) const;

    // Equivalent du meme symbole dans l'autre norme, pour la bascule de profil.
    const SymbolDefinition *counterpart(const QString &id, const QString &norm) const;

    QStringList ids() const;
    QStringList categories(const QString &norm = QString()) const;
    QList<const SymbolDefinition *> byCategory(const QString &category,
                                               const QString &norm = QString()) const;
    QList<const SymbolDefinition *> search(const QString &text,
                                           const QString &norm = QString()) const;
    QList<const SymbolDefinition *> all() const;

    int count() const { return int(m_definitions.size()); }
    bool isEmpty() const { return m_definitions.isEmpty(); }

    // Fusionne une autre bibliotheque. Les definitions embarquees dans un
    // projet ecrasent celles du disque : un dossier archive doit se rouvrir
    // exactement tel qu'il a ete dessine, meme si la bibliotheque a evolue.
    void merge(const SymbolLibrary &other, bool overwrite = true);

private:
    QMap<QString, SymbolDefinition> m_definitions;              // id -> definition
    QHash<QString, QStringList> m_byLogicalId;                  // logicalId -> ids
};

} // namespace dsn
