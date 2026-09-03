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

#include <QFont>
#include <QHash>
#include <QMap>
#include <QPainter>
#include <QSet>

namespace dsn {

class Netlist;
class SymbolInstance;
class Wire;
class Label;
class DimensionItem;
struct TitleBlockCell;
struct WireType;

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

    // Renvois de folio, par identifiant d'etiquette. Calcules hors du peintre
    // (voir rules/crossref) : le renvoi se deduit du dessin et n'est jamais
    // stocke, comme la netlist dont il derive.
    void setCrossReferences(const QHash<QString, QString> &byLabel) { m_crossRefs = byLabel; }
    void clearCrossReferences() { m_crossRefs.clear(); }

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

    // Traduction d'un type de fil vers QtGui. Le coeur ne connait ni QColor
    // ni Qt::PenStyle — c'est ici, ou QtGui est deja lie, que la couleur
    // 0xRRGGBB et le mot-cle de style redeviennent un stylo.
    static QColor wireTypeColor(const WireType &type);
    static Qt::PenStyle wireTypePenStyle(const WireType &type);

    // Texte d'une hauteur donnee en millimetres, quelle que soit l'echelle du
    // painter : la hauteur de capitale d'un texte cote ne depend pas du zoom.
    static void drawTextMm(QPainter &painter, const QPointF &at, const QString &text,
                           double heightMm, Primitive::Align align = Primitive::Align::Left,
                           double rotationDegrees = 0.0);
    static QRectF textBoundsMm(const QPainter &painter, const QPointF &at, const QString &text,
                               double heightMm, Primitive::Align align = Primitive::Align::Left);

    // Largeur reelle d'un texte, sans peintre. Les couches qui composent une
    // mise en page — la pose d'un rapport, par exemple — vivent hors de QtGui
    // et n'ont que des estimations ; celle-ci leur donne la vraie mesure.
    static double textWidthMm(const QFont &base, const QString &text, double heightMm);

private:
    QPen pen(const QColor &color, double width) const;

public:
    // Le style de trait d'une primitive, traduit en style de plume Qt. Le
    // motif est donne en MULTIPLES DE L'EPAISSEUR par Qt : un trait fin
    // aurait sinon des tirets minuscules et un trait epais des tirets
    // enormes. On impose donc un motif en millimetres, pour qu'un cadre
    // d'armoire ait le meme pointille quelle que soit sa plume.
    static void applyStroke(QPen &pen, Primitive::Stroke stroke);

    // LE PAS REELLEMENT TRACE, a partir du pas nominal et de l'echelle.
    //
    // Une grille dont les marques tombent a trois pixels l'une de l'autre
    // n'est plus une grille, c'est un voile gris ; et le garde-fou de densite
    // l'abandonnait alors entierement, ce qui est pire. Le pas double donc
    // jusqu'a respirer. Les marques restent un sous-ensemble EXACT des points
    // d'accrochage — jamais un point ou l'on ne peut pas se poser.
    //
    // Fonction pure, et publique, pour qu'un test lise la regle au lieu de
    // compter des pixels : c'est le seul moyen de prouver qu'elle s'applique.
    static double displayGridStep(double nominalStep, double pixelsPerMm);
    static constexpr double kMinGridPixels = 7.0;

private:
    void paintSymbol(QPainter &painter, const SymbolInstance &symbol) const;
    void paintWire(QPainter &painter, const Wire &wire) const;
    void paintLabel(QPainter &painter, const Label &label) const;
    void paintDimension(QPainter &painter, const DimensionItem &dimension) const;
    void paintTitleBlockCell(QPainter &painter, const TitleBlockCell &cell,
                             const QMap<QString, QString> &values, const Folio &folio) const;
    void paintTitleBlockTable(QPainter &painter, const TitleBlockCell &cell,
                              const Folio &folio) const;
    QColor colorFor(const Entity &entity, const QColor &base) const;

    const Project &m_project;
    RenderStyle m_style;
    QSet<QString> m_selection;
    QSet<QString> m_highlight;
    QVector<QPointF> m_unconnected;
    QHash<QString, QString> m_crossRefs;
};

} // namespace dsn
