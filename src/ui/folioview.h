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
#include "core/wiretools.h"
#include "core/wiretype.h"
#include "document.h"
#include "render/renderstyle.h"
#include "rules/plc.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <optional>

namespace dsn {

class FolioView : public QWidget
{
    Q_OBJECT

public:
    // Les outils modaux. Les cinq derniers sont les formes du panneau
    // « Dessin » d'AutoCAD : elles ne conduisent pas le courant, elles
    // annotent — un encadre de zone, un reperage, un fond de plan. Elles
    // produisent des GraphicItem, jamais des fils.
    enum class Tool {
        Select, Wire, Symbol, Junction, Label, Text, Trim, Extend,
        Line, Rectangle, Circle, Arc, Polyline, Polygon
    };
    Q_ENUM(Tool)

    explicit FolioView(Document *document, QWidget *parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);

    // Symbole arme pour le prochain clic, avec son orientation courante.
    // Un prototype facultatif : ses champs, son repere et son verrou sont
    // recopies sur l'instance posee. C'est ce qui permet a une boite
    // d'insertion — celle des modules d'automate, par exemple — de tout
    // regler avant la pose, et donc de tenir dans une seule annulation.
    void setPendingSymbol(const QString &definitionId,
                          const SymbolInstance *prototype = nullptr);
    QString pendingSymbol() const { return m_pendingSymbol; }

    void setLabelScope(Label::Scope scope) { m_labelScope = scope; }
    Label::Scope labelScope() const { return m_labelScope; }

    // Role des renvois a venir. Une fleche de signal est inter-folios par
    // construction : choisir un role force la portee projet.
    void setLabelRole(Label::Role role);
    Label::Role labelRole() const { return m_labelRole; }

    // Type des fils a venir. Comme dans AutoCAD Electrical, le type courant
    // s'arme une fois puis vaut pour tous les fils qu'on trace ensuite.
    void setCurrentWireType(const QString &id) { m_currentWireType = id; }
    QString currentWireType() const { return m_currentWireType; }

    // FIL MULTIPLE : le trace suivant pose N conducteurs paralleles au lieu
    // d'un. Le geste reste celui du fil — ortho, accrochages, cote tapee : le
    // bus ne redecrit rien, il multiplie ce qui a ete trace.
    //
    // L'armement dure tant que l'outil Fil reste choisi, comme le type de
    // fil. Le rendre a usage unique obligerait a rouvrir la boite entre
    // chaque depart d'un tableau ; le laisser survivre a un changement
    // d'outil ferait tracer un bus a qui ne demandait qu'un fil.
    void setBus(const BusSpec &spec);
    void clearBus() { m_bus.reset(); }
    bool busArmed() const { return m_bus.has_value(); }
    BusSpec bus() const { return m_bus.value_or(BusSpec()); }

    // Applique un type de fil aux fils deja traces de la selection. Le
    // selecteur du ruban n'arme que le trace a venir : sans cette commande,
    // changer le type d'un depart deja dessine demande de le retracer.
    int applyWireTypeToSelection(const QString &wireTypeId);

    // Fixe ou libere les reperes de la selection — le geste qu'on fait juste
    // avant de relancer la renumerotation, pour dire ce qu'elle n'a pas le
    // droit de bousculer.
    int setSelectionTagsLocked(bool locked);

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

    // Zoom fenetre et zoom precedent, comme ZOOM W et ZOOM P d'AutoCAD.
    // Le zoom precedent depile les vues successives : c'est le filet de
    // securite quand on s'est perdu dans un folio dense.
    // Saisie de cote au clavier pendant un geste : taper « 50 » pose le point
    // a 50 mm dans la direction visee, « @10,5 » a un decalage donne. C'est ce
    // qui separe dessiner de pointer.
    bool typing() const { return m_typing; }
    QString typedText() const { return m_typed; }
    // Applique la saisie en cours comme si l'on avait cliqué. Expose pour les
    // tests et pour la ligne de commande.
    bool commitTypedEntry();

    void beginZoomWindow();
    void zoomToRect(const QRectF &sceneRect);
    void zoomPrevious();
    bool canZoomPrevious() const { return !m_viewHistory.isEmpty(); }

    const QSet<QString> &selection() const { return m_selection; }
    void setSelection(const QSet<QString> &ids);
    void selectAll();
    void clearSelection();
    bool hasSelection() const { return !m_selection.isEmpty(); }

    void deleteSelection();
    void rotateSelection(bool clockwise);

    // DEPLACER : un point de base puis un point d'arrivee, comme MOVE. Le
    // deplacement au glisser existe deja ; celui-ci sert quand la distance
    // compte, parce que les deux points s'accrochent au dessin.
    void beginMoveSelection();

    // DECALER : copie les fils selectionnes parallelement, a la distance
    // donnee, du cote ou l'on clique. C'est ainsi qu'on double un depart ou
    // qu'on ajoute un conducteur le long d'un existant.
    void beginOffset(double distanceMm);

    // SCOOT : glisser un appareil le long de son fil, sans jamais le
    // detacher. C'est le geste de correction le plus frequent d'AutoCAD
    // Electrical, et il est contraint par construction.
    void beginScoot();

    // DEPLACER UN APPAREIL : librement, mais en emmenant les fils qui s'y
    // raccordent. Sans eux l'appareil se retrouve debranche en silence.
    void beginMoveComponent();

    // ETIRER : une fenetre de capture, puis deux points. Les sommets pris
    // dans la fenetre suivent, les autres restent — c'est ainsi qu'on
    // rallonge un barreau sans detacher ce qui y est raccorde.
    void beginStretch();

    // ECHELLE (SCALE) : point de base, puis facteur. Le facteur se prend a la
    // souris — la selection suit le curseur — ou se tape, ce qui est le seul
    // moyen d'obtenir exactement 2 ou 0,5.
    void beginScale();

    // COUPURE (BREAK) : couper un fil au point clique.
    void beginCut();

    // JOINDRE (JOIN) : souder les fils colineaires de la selection.
    void joinSelectedWires();

    // MESURER — les utilitaires d'AutoCAD. Une mesure ne modifie rien et ne
    // laisse rien : elle se lit dans la barre d'etat et dans l'historique de
    // la ligne de commande, puis disparait. Coter un plan est un autre geste,
    // qui pose une entite.
    int polygonSides() const { return m_polygonSides; }
    void setPolygonSides(int sides) { m_polygonSides = qBound(3, sides, 64); }

    // PANORAMIQUE (PAN). Le bouton du milieu et la barre d'espace le font
    // deja, mais l'un et l'autre sont des gestes qu'on ne devine pas. Armer
    // le mode donne au panoramique une commande, comme chez AutoCAD — et
    // « P » y est deja pris par Prolonger, d'ou l'alias PAN.
    void beginPan();

    void beginMeasureDistance();
    void beginMeasureArea();

    bool hasPendingGesture() const { return m_pending != Pending::None; }

    // Le conseil du folio vide. Sa geometrie est publique parce qu'elle porte
    // une promesse verifiable — etre cale au centre de la feuille et en
    // occuper une part fixe — et qu'un test doit pouvoir la lire sans compter
    // des pixels.
    bool showsEmptyHint() const;
    QRectF emptyHintRect() const;

    // Part de la HAUTEUR de la feuille occupee par le conseil. C'est le seul
    // chiffre a toucher pour le rendre plus ou moins discret : tout le reste
    // du bloc est compose en unites de dessin et suit ce facteur.
    static constexpr double kHintSheetFraction = 0.10;
    void mirrorSelection();
    void nudgeSelection(const QPointF &deltaMm);
    void copySelection();
    // Coller. Le re-reperage est le comportement par defaut : un depart moteur
    // colle en gardant KM1 fait un dessin juste et une nomenclature fausse,
    // et l'erreur ne se voit qu'au cablage.
    //
    // `keepTags` sert au cas inverse, tout aussi reel : deplacer un circuit
    // d'un folio a l'autre, ou le meme appareil doit garder son identite.
    void pasteClipboard(bool keepTags = false);
    void pasteClipboardKeepingTags() { pasteClipboard(true); }
    bool hasClipboard() const { return !m_clipboard.empty(); }

    // La base des modules d'automates, pour que le collage sache deplacer une
    // carte copiee vers un emplacement libre. Le canevas ne s'en sert a rien
    // d'autre : c'est la fenetre principale qui la detient.
    void setPlcDatabase(const PlcDatabase *plc) { m_plc = plc; }

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
    // Clic droit sans geste en cours : la fenetre principale compose le menu,
    // pour qu'il reprenne ses propres actions avec leurs raccourcis.
    void contextMenuRequested(const QPoint &globalPos);
    void selectionChanged();
    // Le presse-papiers vient de changer : les deux commandes de collage se
    // grisent quand il est vide, et rien d'autre ne les reveille.
    void clipboardChanged();
    void zoomChanged(double pixelsPerMm);
    void snapSettingsChanged();
    void cursorMoved(const QPointF &positionMm, const QString &zone);
    void toolChanged(FolioView::Tool tool);
    void statusMessage(const QString &message);
    // Le resultat d'une mesure, pour l'historique de la ligne de commande :
    // une valeur qu'on relit vaut mieux qu'une valeur qui s'efface au bout de
    // quelques secondes.
    void measured(const QString &report);
    void entityActivated(const QString &entityId);
    // Double-clic dans le vide : il n'y a rien a editer sinon le folio
    // lui-meme, et c'est ce que la fenetre principale en fait.
    void folioActivated();
    // Un symbole vient d'etre pose. La fenetre principale decide d'ouvrir ou
    // non la boite du composant : le catalogue et le reglage lui appartiennent.
    void componentPlaced(const QString &entityId);

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

    // Poignee d'edition directe. AutoCAD en pose sur chaque point
    // remarquable d'une entite selectionnee : on tire dessus au lieu de
    // passer par une commande, et c'est le geste le plus utilise du logiciel.
    struct Grip {
        enum class Kind { Vertex, SegmentMid, Insertion };
        QPointF point;
        QString entityId;
        Kind kind = Kind::Insertion;
        int index = -1; // sommet pour Vertex, premier sommet du segment sinon
    };

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

    // Repere d'alignement sous le curseur, quand aucun point du dessin ne
    // l'emporte. C'est le reperage d'accrochage d'AutoCAD : le curseur se
    // pose sur un trait issu d'un point acquis, pas sur la geometrie.
    std::optional<TrackHit> resolveTrack(const QPointF &scenePoint) const;

    // Acquisition au survol : rester sur un point d'accrochage le retient,
    // y revenir l'oublie. C'est le geste d'AutoCAD, sans clic ni modificateur.
    void updateAcquisition(const QPointF &scenePoint);

    // Point retenu : l'accrochage s'il y en a un, sinon la contrainte de
    // direction, sinon le curseur brut.
    QPointF snap(const QPointF &scenePoint) const;

    // Origine du geste en cours, s'il y en a une.
    const QPointF *gestureOrigin() const;
    QString gestureExclusion() const;
    // Vrai quand la contrainte de direction (ortho, polaire) a un sens pour
    // l'outil courant.
    bool directionConstrained() const;

    Entity *entityAt(const QPointF &scenePoint) const;
    // Deux modes de selection rectangulaire, comme dans AutoCAD :
    // de gauche a droite, la « fenetre » ne prend que ce qu'elle contient
    // entierement ; de droite a gauche, la « capture » prend tout ce qu'elle
    // effleure. Le sens du geste decide, et la couleur du cadre l'annonce.
    QSet<QString> entitiesIn(const QRectF &sceneRect, bool crossing) const;
    bool entityTouchesRect(const Entity &entity, const QRectF &rect) const;
    // Etend une selection a tous les membres des groupes qu'elle touche.
    QSet<QString> expandToGroup(const QSet<QString> &ids) const;

    void beginWireAt(const QPointF &point);

    // Trace de forme en cours. Un vecteur separe de m_wirePoints : un fil se
    // termine par une entite electrique, une forme par une entite graphique,
    // et melanger les deux etats ferait poser un fil avec l'outil cercle.
    QVector<QPointF> m_shapePoints;
    int m_polygonSides = 6;
    void placeShapePoint(const QPointF &point);
    void commitShape();
    void paintShapePreview(QPainter &painter) const;
    // La forme que le trace en cours produirait, ou rien si elle n'est pas
    // encore definie. Sert au trace de l'apercu comme a la pose : une seule
    // construction, donc l'apercu ne peut pas mentir sur le resultat.
    std::optional<Primitive> pendingShape(const QPointF &cursor) const;
    void commitWire();
    void cancelPending();
    void placeSymbolAt(const QPointF &point);
    void placeJunctionAt(const QPointF &point);
    void placeLabelAt(const QPointF &point);
    void placeTextAt(const QPointF &point);
    void trimAt(const QPointF &point);
    void extendAt(const QPointF &point);

    // Ajoute un point de jonction la ou une extremite de fil vient se poser au
    // milieu d'un autre fil : la connexion existe deja electriquement, le point
    // la rend visible.
    void addImplicitJunctions(const Wire &wire);

    void paintPendingWire(QPainter &painter) const;
    void paintPendingSymbol(QPainter &painter) const;
    void paintSnapFeedback(QPainter &painter) const;
    void paintTracking(QPainter &painter) const;
    void paintPolarGuide(QPainter &painter) const;
    void paintRubberBand(QPainter &painter) const;
    void paintGrips(QPainter &painter) const;
    // Le reticule et la saisie dynamique se tracent en pixels, pas en
    // millimetres : ce sont des reperes d'ecran, pas des elements du dessin.
    void paintCrosshair(QPainter &painter) const;
    void paintDynamicInput(QPainter &painter) const;
    // Le point que designerait un clic maintenant : le curseur accroche, ou
    // la cote tapee si l'on est en train d'en saisir une.
    QPointF committedPoint() const;
    bool handleTypedKey(QKeyEvent *event);
    void cancelTyping();

    void pushViewState();
    void rebuildGrips();
    int gripAt(const QPointF &scenePoint) const;
    void dragGripTo(const QPointF &target);
    void paintEmptyHint(QPainter &painter, const Folio &folio) const;

    // Une etape du conseil : la touche, et ce qu'elle fait.
    struct HintStep {
        QString key;
        QString what;
    };
    static QVector<HintStep> emptyHintSteps();

    // La mise en page du conseil, partagee par le trace et par emptyHintRect :
    // deux calculs finiraient par decrire deux blocs differents.
    //
    // Le bloc est compose en UNITES DE DESSIN, puis mis a l'echelle de la
    // feuille. Le composer en pixels liait sa taille a la fenetre : il
    // paraissait enorme sur une feuille dezoomee et minuscule sur une feuille
    // zoomee, alors qu'il doit garder la meme proportion partout.
    struct EmptyHintLayout {
        QRectF design;     // le bloc en unites de composition, origine (0,0)
        QRectF block;      // le meme, mis a l'echelle et pose dans la vue
        double scale = 1.0;
        double keyWidth = 0.0;
        double textWidth = 0.0;
        double lineHeight = 0.0;
    };
    EmptyHintLayout layoutEmptyHint(const Folio &folio) const;

    static constexpr double kHintTitleHeight = 30.0;
    static constexpr double kHintLeadHeight = 22.0;
    static constexpr double kHintLeadGap = 20.0;
    static constexpr double kHintColumnGap = 18.0;
    static constexpr double kHintCapPadding = 9.0;


    // Gestes en deux ou trois clics, a la maniere de la ligne de commande
    // d'AutoCAD : la vue attend un point, puis un autre.
    enum class Pending {
        None, MoveBase, MoveTarget, OffsetSide, StretchBase, StretchTarget,
        ScootTarget, ComponentTarget, ScaleBase, ScaleTarget, CutTarget,
        MeasureDistance, MeasureArea,
    };
    // L'appareil designe par Scoot ou par le deplacement d'appareil, et son
    // point de depart.
    QString m_componentId;
    QPointF m_componentStart;
    std::optional<QPointF> m_scootAxis;

    // Echelle : le rayon de reference, fige au clic du point de base. Poser
    // le curseur sur le coin de la selection donne le facteur 1, deux fois
    // plus loin donne 2 — un reperage previsible, contrairement au facteur
    // en unites de dessin d'AutoCAD, qui depend de l'echelle du folio.
    double m_scaleRadius = 0.0;
    double m_scaleFactor = 1.0;
    void applyScale(double factor);

    // Points de la mesure en cours. Deux pour une distance, n pour une
    // surface.
    QVector<QPointF> m_measurePoints;
    void paintMeasure(QPainter &painter) const;
    QString measureReport() const;

    // Trouve l'appareil a manipuler dans la selection, ou rien.
    SymbolInstance *selectedComponent() const;
    bool handlePendingClick(const QPointF &scenePoint);
    // Un point designe, par clic ou par cote tapee. Les deux chemins doivent
    // faire exactement la meme chose, sinon dessiner au clavier et dessiner a
    // la souris divergent.
    bool applyPointAt(const QPointF &scenePoint);
    void placeAt(const QPointF &scenePoint);
    void applyOffset(const QPointF &sidePoint);
    void applyStretch(const QPointF &delta);
    void paintPendingGesture(QPainter &painter) const;

    void updateUnconnectedPins();
    void updateCrossReferences();
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
    QHash<QString, QString> m_crossRefs;

    // Geste en cours
    enum class Drag { None, Pan, Move, Rubber, GripEdit, ZoomWindow, StretchWindow };
    Drag m_drag = Drag::None;
    QPointF m_dragStartWidget;
    QPointF m_dragStartScene;
    QPointF m_dragLastScene;
    QRectF m_rubber;
    bool m_rubberCrossing = false;

    QVector<Grip> m_grips;
    int m_hotGrip = -1;      // poignee survolee
    int m_draggedGrip = -1;  // poignee tiree
    bool m_spaceHeld = false;
    bool m_panArmed = false;
    bool m_cursorInside = false;
    bool m_zoomWindowArmed = false;

    struct ViewState { double scale; QPointF pan; };
    QVector<ViewState> m_viewHistory;
    bool m_movedSinceCommit = false;

    Pending m_pending = Pending::None;
    QPointF m_moveBase;
    double m_offsetDistance = 2.5;
    bool m_stretchArmed = false;    // en attente de la fenetre de capture
    QRectF m_stretchWindow;         // fenetre retenue, en millimetres

    QVector<QPointF> m_wirePoints;
    QString m_pendingSymbol;
    std::optional<SymbolInstance> m_pendingPrototype;
    Placement m_pendingPlacement;
    Label::Scope m_labelScope = Label::Scope::Folio;
    Label::Role m_labelRole = Label::Role::Plain;
    QString m_currentWireType = WireTypeSet::defaultId();

    // Saisie de cote en cours. Le texte est retenu tel qu'il est frappe : on
    // ne l'interprete qu'a la validation, pour que « 1 » puis « 0 » ne pose
    // pas un point a un millimetre avant d'en poser un a dix.
    bool m_typing = false;
    QString m_typed;

    QPointF m_cursorMm;
    std::optional<SnapHit> m_snapHit;
    std::optional<TrackHit> m_trackHit;

    // Acquisition au survol. Le point n'est retenu qu'apres un temps d'arret :
    // sans ce delai, traverser un dessin dense acquerrait tout sur son passage.
    QTimer *m_acquireTimer = nullptr;
    std::optional<SnapHit> m_hoverCandidate;
    // QVector exige un type copiable, ce que EntityPtr n'est pas.
    std::optional<BusSpec> m_bus;
    std::vector<EntityPtr> m_clipboard;
    const PlcDatabase *m_plc = nullptr;
};

} // namespace dsn
