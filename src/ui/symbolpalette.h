// Palette de symboles : recherche, categories, apercu.
//
// C'est le panneau le plus sollicite de la journee. Deux choses comptent : la
// recherche doit trouver sur le nom courant comme sur le mot-cle metier, et
// l'apercu doit etre le vrai trace du symbole, pas une icone approchante.
#pragma once

#include "core/symbollibrary.h"
#include "render/renderstyle.h"

#include <QIcon>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace dsn {

class SymbolPalette : public QWidget
{
    Q_OBJECT

public:
    explicit SymbolPalette(QWidget *parent = nullptr);

    void setLibrary(const SymbolLibrary *library);
    void setNorm(const QString &norm);
    QString norm() const { return m_norm; }

    QString currentDefinitionId() const;

    // Apercu d'une definition, dessine avec le meme peintre que le canevas.
    static QIcon renderIcon(const SymbolDefinition &definition, int pixels,
                            const RenderStyle &style);

Q_SIGNALS:
    void symbolChosen(const QString &definitionId);
    void symbolActivated(const QString &definitionId);

private:
    void rebuildCategories();
    void rebuildList();

    const SymbolLibrary *m_library = nullptr;
    QString m_norm = QStringLiteral("IEC");

    QLineEdit *m_search = nullptr;
    QComboBox *m_category = nullptr;
    QListWidget *m_list = nullptr;
};

} // namespace dsn
