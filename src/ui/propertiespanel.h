// Inspecteur de proprietes.
//
// Le panneau se reconstruit a chaque changement de selection. Toute edition
// passe par une commande d'annulation : rien ne modifie le document
// directement, sans quoi l'historique se desynchronise du dessin.
#pragma once

#include "core/documentcommands.h"
#include "core/entities.h"
#include "document.h"

#include <QSet>
#include <QWidget>

class QFormLayout;
class QLabel;
class QScrollArea;

namespace dsn {

class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanel(Document *document, QWidget *parent = nullptr);

    void setSelection(const QSet<QString> &ids);

Q_SIGNALS:
    void statusMessage(const QString &message);

private:
    void rebuild();
    void buildFolioForm(QFormLayout *form);
    void buildSymbolForm(QFormLayout *form, SymbolInstance *symbol);
    void buildWireForm(QFormLayout *form, Wire *wire);
    void buildTextForm(QFormLayout *form, TextItem *text);
    void buildLabelForm(QFormLayout *form, Label *label);
    void buildJunctionForm(QFormLayout *form, Junction *junction);
    void buildMultiForm(QFormLayout *form);

    // Applique une mutation a une entite par instantane avant/apres.
    template <typename T, typename Mutator>
    void modify(T *entity, const QString &text, Mutator mutate, int mergeId = NoMerge);

    Document *m_document = nullptr;
    QSet<QString> m_selection;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QLabel *m_header = nullptr;
    bool m_rebuilding = false;
};

} // namespace dsn
