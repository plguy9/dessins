// Peintre de folio.
//
// C'est le seul endroit du logiciel qui sait a quoi ressemble un schema. La
// vue a l'ecran, l'apercu avant impression et le PDF passent tous par lui,
// avec le meme QPainter : il n'y a donc aucun endroit ou l'ecran et le papier
// peuvent diverger.
//
// Le peintre travaille en millimetres. C'est a l'appelant de poser la
// transformation qui convertit les millimetres en pixels ou en points.
#pragma once

#include "core/project.h"
#include "core/snapengine.h"
#include "renderstyle.h"

#include <QPainter>
#include <QSet>

namespace dsn {

class Netlist;
class SymbolInstance;
class Wire;
class Label;

class FolioPainter
{
public:
    FolioPainter(const Project &project, RenderStyle style = RenderStyle::screen());

    void setStyle(const RenderStyle &style) { m_style = style; }
    const RenderStyle &style() const { return m_style; }

    void setSelection(const QSet<QString> &entityIds) { m_selection = entityIds; }
    void clearSelection() { m_selection.clear(); }

    // Met en evidence un potentiel : c'est le geste qui permet de suivre un
    // fil a travers un folio dense.
    void setHighlightedEntities(const QSet<QString> &entityIds) { m_highlight = entityIds; }
    void clearHighlight() { m_highlight.clear(); }

    // Broches libres, signalees d'une croix. Renseigne depuis la netlist.
    void setUnconnectedPins(const QVector<QPointF> &points) { m_unconnected = points; }

    // Peint le folio complet. clipMm limite le trace a une zone du document,
    // ce qui evite de parcourir tout le folio a chaque rafraichissement.
    void paint(QPainter &painter, const Folio &folio, const QRectF &clipMm = QRectF()) const;

    // Etapes separees, utiles a la vue qui gere ses propres couches.
    void paintSheet(QPainter &painter, const Folio &folio) const;
    void paintGrid(QPainter &painter, const Folio &folio, const QRectF &clipMm) const;
    void paintFrame(QPainter &painter, const Folio &folio) const;
    void paintTitleBlock(QPainter &painter, const Folio &folio) const;
    void paintEntities(QPainter &painter, const Folio &folio, const QRectF &clipMm) const;
    void paintEntity(QPainter &painter, const Entity &entity) const;
    void paintDecorations(QPainter &painter, const Folio &folio) const;

    // Dessine un symbole isole, pour la palette et l'editeur de symboles.
    static void paintDefinition(QPainter &painter, const SymbolDefinition &definition,
                                const RenderStyle &style, bool withPins = true);

    // Trace une primitive. Un seul chemin pour les annotations et pour le
    // graphisme des symboles.
    static void paintPrimitive(QPainter &painter, const Primitive &primitive);

    // Marqueur d'accrochage. Les formes reprennent celles d'AutoCAD : carre
    // pour une extremite, triangle pour un milieu, cercle pour un centre.
    // Elles sont aussi normatives que les symboles — c'est a leur silhouette
    // qu'un dessinateur reconnait ce a quoi il est en train de s'accrocher.
    static void paintSnapMarker(QPainter &painter, SnapMode mode, const QPointF &point,
                                double sizeMm, const QColor &color);

    // Texte d'une hauteur donnee en millimetres, quelle que soit l'echelle du
    // painter : la hauteur de capitale d'un texte cote ne depend pas du zoom.
    static void drawTextMm(QPainter &painter, const QPointF &at, const QString &text,
                           double heightMm, Primitive::Align align = Primitive::Align::Left,
                           double rotationDegrees = 0.0);
    static QRectF textBoundsMm(const QPainter &painter, const QPointF &at, const QString &text,
                               double heightMm, Primitive::Align align = Primitive::Align::Left);

private:
    QPen pen(const QColor &color, double width) const;
    void paintSymbol(QPainter &painter, const SymbolInstance &symbol) const;
    void paintWire(QPainter &painter, const Wire &wire) const;
    void paintLabel(QPainter &painter, const Label &label) const;
    QColor colorFor(const Entity &entity, const QColor &base) const;

    const Project &m_project;
    RenderStyle m_style;
    QSet<QString> m_selection;
    QSet<QString> m_highlight;
    QVector<QPointF> m_unconnected;
};

} // namespace dsn
