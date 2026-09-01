// Le canevas de dessin.
//
// C'est un QWidget qui peint avec FolioPainter, pas une QGraphicsView. Ce
// choix decoule directement de la decision de rendu : passer par une scene
// graphique obligerait a envelopper chaque entite dans un QGraphicsItem et
// donc a redecrire le trace, ce qui rouvrirait l'ecart entre l'ecran et le
// papier que FolioPainter existe precisement pour fermer. Le prix a payer est
// la selection, le zoom et le panoramique a ecrire soi-meme.
#pragma once

#include "core/entities.h"
#include "core/snapengine.h"
#include "document.h"
#include "render/renderstyle.h"

#include <QSet>
#include <QWidget>

namespace dsn {

class FolioView : public QWidget
{
    Q_OBJECT

public:
    enum class Tool { Select, Wire, Symbol, Junction, Label, Text };
    Q_ENUM(Tool)

    explicit FolioView(Document *document, QWidget *parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);

    // Symbole arme pour le prochain clic, avec son orientation courante.
    void setPendingSymbol(const QString &definitionId);
    QString pendingSymbol() const { return m_pendingSymbol; }

    void setLabelScope(Label::Scope scope) { m_labelScope = scope; }
    Label::Scope labelScope() const { return m_labelScope; }

    const RenderStyle &style() const { return m_style; }
    void setStyle(const RenderStyle &style);

    double gridStep() const { return m_style.gridStep; }
    void setGridStep(double step);
    void setGridVisible(bool visible);

    // Le moteur d'accrochage est expose : la barre d'etat et la boite de
    // reglages agissent dessus directement, sans que la vue ait a relayer
    // une bascule par methode.
    SnapEngine &snapEngine() { return m_snapEngine; }
    const SnapEngine &snapEngine() const { return m_snapEngine; }
    void snapSettingsTouched();   // a appeler apres modification du moteur

    double zoom() const { return m_scale; }
    void setZoom(double pixelsPerMm, const QPointF &anchorPx = QPointF());
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomActual();

    const QSet<QString> &selection() const { return m_selection; }
    void setSelection(const QSet<QString> &ids);
    void selectAll();
    void clearSelection();
    bool hasSelection() const { return !m_selection.isEmpty(); }

    void deleteSelection();
    void rotateSelection(bool clockwise);
    void mirrorSelection();
    void nudgeSelection(const QPointF &deltaMm);
    void copySelection();
    void pasteClipboard();

    // Met en evidence le potentiel de la selection : le geste qui permet de
    // suivre un fil a travers un folio dense.
    void highlightNetOfSelection();
    void clearHighlight();

    QPointF cursorMm() const { return m_cursorMm; }

    // Conversions entre le folio et le widget. Publiques : les tests et les
    // outils de capture en ont besoin pour viser un point du dessin.
    QPointF mapToScene(const QPointF &widgetPoint) const { return toScene(widgetPoint); }
    QPointF mapFromScene(const QPointF &scenePoint) const { return toWidget(scenePoint); }

Q_SIGNALS:
    void selectionChanged();
    void zoomChanged(double pixelsPerMm);
    void snapSettingsChanged();
    void cursorMoved(const QPointF &positionMm, const QString &zone);
    void toolChanged(FolioView::Tool tool);
    void statusMessage(const QString &message);
    void entityActivated(const QString &entityId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QPointF toScene(const QPointF &widgetPoint) const;
    QPointF toWidget(const QPointF &scenePoint) const;
    QRectF visibleSceneRect() const;
    // Rayon d'accrochage, defini a l'ecran : il doit rester confortable a
    // tous les niveaux de zoom, comme la fenetre d'accrochage d'AutoCAD.
    double aperture() const;

    // Resout l'accrochage sous un point. `from` alimente les modes qui ont
    // besoin d'une origine (perpendiculaire) ; l'entite en cours de trace
    // est ecartee pour qu'elle ne s'accroche pas a elle-meme.
    std::optional<SnapHit> resolveSnap(const QPointF &scenePoint) const;

    // Point retenu : l'accrochage s'il y en a un, sinon la contrainte de
    // direction, sinon le curseur brut.
    QPointF snap(const QPointF &scenePoint) const;

    // Origine du geste en cours, s'il y en a une.
    const QPointF *gestureOrigin() const;
    QString gestureExclusion() const;

    Entity *entityAt(const QPointF &scenePoint) const;
    QSet<QString> entitiesIn(const QRectF &sceneRect) const;

    void beginWireAt(const QPointF &point);
    void commitWire();
    void cancelPending();
    void placeSymbolAt(const QPointF &point);
    void placeJunctionAt(const QPointF &point);
    void placeLabelAt(const QPointF &point);
    void placeTextAt(const QPointF &point);

    // Ajoute un point de jonction la ou une extremite de fil vient se poser au
    // milieu d'un autre fil : la connexion existe deja electriquement, le point
    // la rend visible.
    void addImplicitJunctions(const Wire &wire);

    void paintPendingWire(QPainter &painter) const;
    void paintPendingSymbol(QPainter &painter) const;
    void paintSnapFeedback(QPainter &painter) const;
    void paintPolarGuide(QPainter &painter) const;
    void paintRubberBand(QPainter &painter) const;
    void paintEmptyHint(QPainter &painter, const Folio &folio) const;

    void updateUnconnectedPins();
    void emitCursor();

    Document *m_document = nullptr;
    RenderStyle m_style = RenderStyle::screen();
    Tool m_tool = Tool::Select;

    double m_scale = 3.0;   // pixels par millimetre
    QPointF m_pan{ 20.0, 20.0 };
    SnapEngine m_snapEngine;
    // L'ajustement au folio a besoin de la taille reelle du widget, que l'on
    // ne connait qu'apres la premiere mise en page : la demande est donc mise
    // en attente jusqu'au premier redimensionnement utile.
    bool m_fitPending = true;

    QSet<QString> m_selection;
    QSet<QString> m_highlight;
    QVector<QPointF> m_unconnectedPins;

    // Geste en cours
    enum class Drag { None, Pan, Move, Rubber };
    Drag m_drag = Drag::None;
    QPointF m_dragStartWidget;
    QPointF m_dragStartScene;
    QPointF m_dragLastScene;
    QRectF m_rubber;
    bool m_spaceHeld = false;
    bool m_movedSinceCommit = false;

    QVector<QPointF> m_wirePoints;
    QString m_pendingSymbol;
    Placement m_pendingPlacement;
    Label::Scope m_labelScope = Label::Scope::Folio;

    QPointF m_cursorMm;
    std::optional<SnapHit> m_snapHit;
    // QVector exige un type copiable, ce que EntityPtr n'est pas.
    std::vector<EntityPtr> m_clipboard;
};

} // namespace dsn
