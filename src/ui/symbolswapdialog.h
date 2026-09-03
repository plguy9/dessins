// « Remplacer le symbole » — le Swap/Update Block d'AutoCAD Electrical.
//
// Un contact NO devient un contact NF, un disjoncteur change de calibre. Sans
// ce geste il faut effacer, reposer, retaper le repere et refaire les deux
// fils — et c'est justement en refaisant les fils qu'on debranche un circuit
// sans s'en apercevoir.
//
// La boite ne choisit qu'un symbole : c'est le canevas qui fait le
// remplacement (FolioView::swapSymbol), en une seule annulation. Elle reprend
// la palette telle quelle plutot que d'en refaire une — on reconnait un
// symbole a sa forme, et ce serait la deuxieme grille de vignettes a tenir.
#pragma once

#include <QDialog>

namespace dsn {

class SymbolLibrary;
class SymbolPalette;

class SymbolSwapDialog : public QDialog
{
    Q_OBJECT

public:
    SymbolSwapDialog(const SymbolLibrary *library, const QString &norm,
                     const QString &currentDefinitionId, const QString &designation,
                     QWidget *parent = nullptr);

    // Vide tant que rien n'est choisi, ou si le choix est le symbole actuel.
    QString chosenDefinitionId() const;

private:
    SymbolPalette *m_palette = nullptr;
    QString m_current;
};

} // namespace dsn
