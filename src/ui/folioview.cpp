#include "folioview.h"

#include "theme.h"

#include "core/componenttools.h"
#include "core/coordinateentry.h"
#include "core/documentcommands.h"
#include "core/edittools.h"
#include "core/wiretools.h"
#include "render/foliopainter.h"
#include "rules/circuitcopy.h"
#include "rules/crossref.h"

#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <numbers>
#include <QWheelEvent>

#include <algorithm>

namespace dsn {

namespace {

constexpr double kMinScale = 0.2;   // pixels par millimetre
constexpr double kMaxScale = 60.0;
constexpr double kPickToleranceMm = 1.2;

} // namespace

FolioView::FolioView(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    // Le reticule remplace le curseur systeme : deux pointeurs superposes se
    // genent, et c'est le reticule qui porte la precision.
    setCursor(Qt::BlankCursor);

    connect(m_document, &Document::changed, this, [this] {
        updateUnconnectedPins();
        updateCrossReferences();
        // Une modification venue d'ailleurs deplace peut-etre ce que les
        // poignees designent : elles doivent suivre.
        if (m_draggedGrip < 0)
            rebuildGrips();
        update();
    });
    connect(m_document, &Document::currentFolioChanged, this, [this] {
        m_selection.clear();
        m_highlight.clear();
        cancelPending();
        zoomToFit();
        Q_EMIT selectionChanged();
    });
    // Acquisition au survol. Le delai est celui d'AutoCAD a quelques
    // dizaines de millisecondes pres : assez court pour que le geste reste
    // fluide, assez long pour ne pas acquerir en traversant.
    m_acquireTimer = new QTimer(this);
    m_acquireTimer->setSingleShot(true);
    m_acquireTimer->setInterval(350);
    connect(m_acquireTimer, &QTimer::timeout, this, [this] {
        if (!m_hoverCandidate || !m_cursorInside)
            return;
        // Le curseur doit toujours etre sur le point : sinon l'utilisateur
        // est deja reparti et acquerir serait une surprise.
        const auto current = resolveSnap(m_cursorMm);
        if (!current || !samePoint(current->point, m_hoverCandidate->point))
            return;

        const bool was = m_snapEngine.isTracked(current->point);
        m_snapEngine.toggleTracked(current->point, current->mode);
        Q_EMIT statusMessage(was ? tr("Repère relâché.")
                                 : tr("Repère acquis : %1. Éloignez le curseur pour "
                                      "suivre son alignement.")
                                           .arg(current->label()));
        m_hoverCandidate.reset();
        update();
    });

    updateUnconnectedPins();
    updateCrossReferences();
}

// --------------------------------------------------------------------------
// Reperes

QPointF FolioView::toScene(const QPointF &widgetPoint) const
{
    return (widgetPoint - m_pan) / m_scale;
}

QPointF FolioView::toWidget(const QPointF &scenePoint) const
{
    return scenePoint * m_scale + m_pan;
}

QRectF FolioView::visibleSceneRect() const
{
    return QRectF(toScene(QPointF(0, 0)), toScene(QPointF(width(), height())));
}

double FolioView::aperture() const
{
    // Dix pixels a l'ecran, comme la fenetre d'accrochage d'AutoCAD, avec un
    // plancher lie a la grille pour rester utilisable en zoom arriere.
    return std::max(m_style.gridStep * 0.5, 12.0 / m_scale);
}

const QPointF *FolioView::gestureOrigin() const
{
    // Le trace de fil fournit une origine : c'est elle qui donne son sens a
    // l'accrochage perpendiculaire et aux contraintes de direction.
    if (!m_wirePoints.isEmpty())
        return &m_wirePoints.last();
    if (!m_shapePoints.isEmpty())
        return &m_shapePoints.last();
    // Un deplacement ou un etirement en a une aussi, des que le point de base
    // est pose : leur second point merite la meme contrainte et la meme cote.
    if (m_pending == Pending::MoveTarget || m_pending == Pending::StretchTarget)
        return &m_moveBase;
    return nullptr;
}

bool FolioView::directionConstrained() const
{
    // Ortho et le suivi polaire ne valent que la ou une droite est le sujet :
    // un fil, une ligne, une polyligne, un deplacement. Appliquee au coin
    // oppose d'un rectangle, la contrainte l'aplatit ; au rayon d'un cercle,
    // elle le force sur un axe ; aux points d'un arc, elle les aligne — et un
    // arc dont les trois points sont alignes n'existe pas.
    //
    // AutoCAD applique la contrainte partout et laisse l'utilisateur eteindre
    // ortho avant chaque arc. Ortho etant allume par defaut chez nous, cela
    // rendrait trois outils sur six inutilisables au premier essai.
    switch (m_tool) {
    case Tool::Rectangle:
    case Tool::Circle:
    case Tool::Arc:
        return false;
    default:
        return true;
    }
}

QString FolioView::gestureExclusion() const
{
    // Rien a exclure aujourd'hui : le fil en cours n'existe pas encore comme
    // entite. La fonction existe pour le jour ou l'edition d'un fil pose
    // permettra de deplacer un de ses sommets.
    return QString();
}

std::optional<SnapHit> FolioView::resolveSnap(const QPointF &scenePoint) const
{
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return std::nullopt;

    // La grille est ecartee de la resolution visuelle : elle accroche
    // partout, et son marqueur permanent sous le curseur serait du bruit.
    SnapEngine engine = m_snapEngine;
    engine.setMode(SnapMode::Grid, false);

    return engine.snap(*folio, m_document->project().library, scenePoint, aperture(),
                       gestureOrigin(), gestureExclusion());
}

std::optional<TrackHit> FolioView::resolveTrack(const QPointF &scenePoint) const
{
    return m_snapEngine.track(scenePoint, aperture(), gestureOrigin());
}

void FolioView::updateAcquisition(const QPointF &scenePoint)
{
    // Comme chez AutoCAD, on n'acquiert que pendant une commande : hors
    // commande, le survol accumulerait des reperes que personne n'a demandes.
    const bool commandRunning = m_tool != Tool::Select || !m_wirePoints.isEmpty()
            || m_pending != Pending::None || m_stretchArmed;
    if (!commandRunning || !m_snapEngine.trackingEnabled()
        || !m_snapEngine.objectSnapEnabled()) {
        m_hoverCandidate.reset();
        m_acquireTimer->stop();
        return;
    }

    const auto hit = resolveSnap(scenePoint);
    if (!hit) {
        m_hoverCandidate.reset();
        m_acquireTimer->stop();
        return;
    }

    // Tant qu'on reste sur le meme point d'accrochage, le compte a rebours
    // court. Des qu'on en change, il repart : c'est le temps d'arret qui
    // distingue « je vise ce point » de « je passe dessus ».
    if (m_hoverCandidate && samePoint(m_hoverCandidate->point, hit->point))
        return;
    m_hoverCandidate = hit;
    m_acquireTimer->start();
}

QPointF FolioView::snap(const QPointF &scenePoint) const
{
    if (const auto hit = resolveSnap(scenePoint))
        return hit->point;

    // Un repere d'alignement passe avant la contrainte de direction : il
    // designe un point, alors que la contrainte ne donne qu'une direction.
    if (const auto track = resolveTrack(scenePoint))
        return track->point;

    // Sans accrochage a un objet, la contrainte de direction prend la main,
    // puis la grille. L'ordre compte : un point du dessin vaut toujours mieux
    // qu'un point calcule.
    if (const QPointF *from = gestureOrigin()) {
        const QPointF constrained = directionConstrained()
                ? m_snapEngine.constrain(*from, scenePoint)
                : scenePoint;
        if (m_snapEngine.gridSnapEnabled())
            return m_snapEngine.snapToGridPoint(constrained);
        return constrained;
    }
    if (m_snapEngine.gridSnapEnabled())
        return m_snapEngine.snapToGridPoint(scenePoint);
    return scenePoint;
}

QPointF FolioView::snapAnnotation(const QPointF &scenePoint) const
{
    // Meme ordre que snap(), ampute de la grille : un point du dessin, puis
    // un repere d'alignement, puis le curseur tel quel.
    if (const auto hit = resolveSnap(scenePoint))
        return hit->point;
    if (const auto track = resolveTrack(scenePoint))
        return track->point;
    return scenePoint;
}

void FolioView::snapSettingsTouched()
{
    m_snapHit.reset();
    Q_EMIT snapSettingsChanged();
    update();
}

// --------------------------------------------------------------------------
// Selection

namespace {

// Une forme creuse se designe par son contour, pas par sa boite. Sans cette
// regle, un encadre de zone pose sur un folio avalerait tous les clics de la
// zone qu'il encadre — et il est justement fait pour en encadrer une.
bool graphicOutlineHit(const Primitive &shape, const QPointF &p, double tolerance)
{
    switch (shape.kind) {
    case Primitive::Kind::Line:
    case Primitive::Kind::Polyline:
        for (int i = 1; i < shape.points.size(); ++i) {
            if (pointOnSegment(p, shape.points.at(i - 1), shape.points.at(i), tolerance))
                return true;
        }
        return false;
    case Primitive::Kind::Rect: {
        if (shape.points.size() < 2)
            return false;
        const QRectF r = normalized(shape.points.first(), shape.points.last());
        if (shape.filled)
            return r.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p);
        const QPointF corners[5] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(),
                                     r.topLeft() };
        for (int i = 1; i < 5; ++i) {
            if (pointOnSegment(p, corners[i - 1], corners[i], tolerance))
                return true;
        }
        return false;
    }
    case Primitive::Kind::Circle:
    case Primitive::Kind::Arc: {
        if (shape.points.isEmpty())
            return false;
        const QPointF c = shape.points.first();
        const double distance = std::hypot(p.x() - c.x(), p.y() - c.y());
        if (shape.filled && shape.kind == Primitive::Kind::Circle)
            return distance <= shape.radius + tolerance;
        if (std::abs(distance - shape.radius) > tolerance)
            return false;
        if (shape.kind == Primitive::Kind::Circle)
            return true;
        // Un arc n'est pas un cercle : le point doit tomber dans le secteur
        // reellement trace, sinon on attrape la partie qui n'existe pas.
        const double angle = std::atan2(-(p.y() - c.y()), p.x() - c.x()) * 180.0
                / std::numbers::pi;
        double offset = std::fmod(angle - shape.startAngle, 360.0);
        if (offset < 0.0)
            offset += 360.0;
        const double span = shape.spanAngle;
        return span >= 0.0 ? offset <= span : offset >= 360.0 + span;
    }
    case Primitive::Kind::Text:
        return shape.bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p);
    }
    return false;
}

} // namespace

Entity *FolioView::entityAt(const QPointF &scenePoint) const
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return nullptr;

    const double tolerance = std::max(kPickToleranceMm, 4.0 / m_scale);

    // Parcours du dessus vers le dessous : le dernier trace est celui qu'on
    // voit, donc celui qu'on attend en cliquant.
    const auto &entities = folio->entities();
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        Entity *entity = it->get();
        if (const auto *wire = dynamic_cast<const Wire *>(entity)) {
            for (int i = 1; i < wire->points.size(); ++i) {
                if (pointOnSegment(scenePoint, wire->points.at(i - 1), wire->points.at(i),
                                   tolerance))
                    return entity;
            }
            continue;
        }
        if (const auto *graphic = dynamic_cast<const GraphicItem *>(entity)) {
            if (graphicOutlineHit(graphic->shape, scenePoint, tolerance))
                return entity;
            continue;
        }
        const QRectF bounds = entity->boundingBox();
        if (!bounds.isNull() && bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                                        .contains(scenePoint))
            return entity;
    }
    return nullptr;
}

bool FolioView::entityTouchesRect(const Entity &entity, const QRectF &rect) const
{
    // Un fil est un trait, pas une boite : tester sa boite englobante
    // attraperait tout ce qui passe dans le rectangle de ses diagonales.
    if (const auto *wire = dynamic_cast<const Wire *>(&entity)) {
        for (int i = 1; i < wire->points.size(); ++i) {
            if (segmentIntersectsRect(wire->points.at(i - 1), wire->points.at(i), rect))
                return true;
        }
        return false;
    }
    const QRectF bounds = entity.boundingBox();
    return !bounds.isNull() && rect.intersects(bounds);
}

QSet<QString> FolioView::entitiesIn(const QRectF &sceneRect, bool crossing) const
{
    QSet<QString> found;
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return found;
    for (const EntityPtr &entity : folio->entities()) {
        const QRectF bounds = entity->boundingBox();
        if (bounds.isNull())
            continue;
        const bool hit = crossing ? entityTouchesRect(*entity, sceneRect)
                                  : sceneRect.contains(bounds);
        if (hit)
            found.insert(entity->id());
    }
    return found;
}

void FolioView::rebuildGrips()
{
    m_grips.clear();
    m_hotGrip = -1;
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return;

    // Au-dela de quelques dizaines d'entites selectionnees, les poignees
    // couvriraient le dessin sans rien apporter : AutoCAD les masque de meme
    // au-dela d'un seuil.
    if (m_selection.size() > 40)
        return;

    for (const QString &id : m_selection) {
        const Entity *entity = folio->entity(id);
        if (!entity)
            continue;

        if (const auto *wire = dynamic_cast<const Wire *>(entity)) {
            for (int i = 0; i < wire->points.size(); ++i)
                m_grips.append({ wire->points.at(i), id, Grip::Kind::Vertex, i });
            for (int i = 1; i < wire->points.size(); ++i) {
                m_grips.append({ (wire->points.at(i - 1) + wire->points.at(i)) / 2.0, id,
                                 Grip::Kind::SegmentMid, i - 1 });
            }
            continue;
        }
        if (const auto *symbol = dynamic_cast<const SymbolInstance *>(entity)) {
            m_grips.append({ symbol->placement.position, id, Grip::Kind::Insertion, -1 });
            continue;
        }
        if (const auto *junction = dynamic_cast<const Junction *>(entity)) {
            m_grips.append({ junction->point, id, Grip::Kind::Insertion, -1 });
            continue;
        }
        if (const auto *label = dynamic_cast<const Label *>(entity)) {
            m_grips.append({ label->point, id, Grip::Kind::Insertion, -1 });
            continue;
        }
        if (const auto *text = dynamic_cast<const TextItem *>(entity)) {
            m_grips.append({ text->placement.position, id, Grip::Kind::Insertion, -1 });
            continue;
        }
    }
}

int FolioView::gripAt(const QPointF &scenePoint) const
{
    // La zone de prise est definie a l'ecran : une poignee doit rester
    // attrapable au zoom arriere sans devenir un piege au zoom avant.
    const double reach = 7.0 / m_scale;
    int best = -1;
    double bestDistance = reach;
    for (int i = 0; i < m_grips.size(); ++i) {
        const double d = std::hypot(m_grips.at(i).point.x() - scenePoint.x(),
                                    m_grips.at(i).point.y() - scenePoint.y());
        if (d <= bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void FolioView::dragGripTo(const QPointF &target)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_draggedGrip < 0 || m_draggedGrip >= m_grips.size())
        return;

    const Grip grip = m_grips.at(m_draggedGrip);
    Entity *entity = folio->entity(grip.entityId);
    if (!entity)
        return;

    auto before = entity->clone();
    auto after = entity->clone();

    if (auto *wire = dynamic_cast<Wire *>(after.get())) {
        if (grip.kind == Grip::Kind::Vertex) {
            if (grip.index < 0 || grip.index >= wire->points.size())
                return;
            wire->points[grip.index] = target;
        } else if (grip.kind == Grip::Kind::SegmentMid) {
            if (grip.index < 0 || grip.index + 1 >= wire->points.size())
                return;
            // Tirer le milieu d'un segment translate le segment entier, comme
            // la poignee mediane d'AutoCAD sur une ligne.
            const QPointF delta = target - grip.point;
            wire->points[grip.index] += delta;
            wire->points[grip.index + 1] += delta;
        }
    } else if (auto *symbol = dynamic_cast<SymbolInstance *>(after.get())) {
        symbol->placement.position = target;
    } else if (auto *junction = dynamic_cast<Junction *>(after.get())) {
        junction->point = target;
    } else if (auto *label = dynamic_cast<Label *>(after.get())) {
        label->point = target;
    } else if (auto *text = dynamic_cast<TextItem *>(after.get())) {
        text->placement.position = target;
    } else {
        return;
    }

    auto command = std::make_unique<ModifyEntityCommand>(m_document->project(), folio->id(),
                                                         std::move(before), std::move(after),
                                                         tr("Déplacer une poignée"));
    // Les etirements successifs d'un meme geste fusionnent : l'utilisateur
    // n'attend qu'une seule entree annulable pour un seul glisser.
    command->setMergeId(MergeMove);
    m_document->push(std::move(command));

    m_grips[m_draggedGrip].point = target;
}

void FolioView::paintGrips(QPainter &painter) const
{
    if (m_grips.isEmpty())
        return;

    const double size = std::max(1.2, 7.0 / m_scale);
    const double h = size / 2.0;

    painter.save();
    for (int i = 0; i < m_grips.size(); ++i) {
        const Grip &grip = m_grips.at(i);
        const bool hot = (i == m_hotGrip) || (i == m_draggedGrip);

        // Bleu au repos, chaud sous le curseur : les couleurs d'AutoCAD, que
        // des millions de dessinateurs lisent sans y penser.
        const QColor fill = hot ? QColor(0xE0, 0x50, 0x40) : QColor(0x3D, 0x7E, 0xC8);
        painter.setPen(QPen(QColor(0xF0, 0xF4, 0xF8), size * 0.12));
        painter.setBrush(fill);

        if (grip.kind == Grip::Kind::SegmentMid) {
            // Le milieu de segment porte un rectangle allonge : on le
            // distingue ainsi d'un sommet, qu'il ne fait pas la meme chose.
            painter.drawRect(QRectF(grip.point.x() - h, grip.point.y() - h * 0.62, size,
                                    size * 0.62));
        } else {
            painter.drawRect(QRectF(grip.point.x() - h, grip.point.y() - h, size, size));
        }
    }
    painter.restore();
}

void FolioView::setSelection(const QSet<QString> &ids)
{
    if (m_selection == ids)
        return;
    m_selection = ids;
    rebuildGrips();
    Q_EMIT selectionChanged();
    update();
}

void FolioView::selectAll()
{
    QSet<QString> all;
    if (const Folio *folio = m_document->currentFolio()) {
        for (const EntityPtr &entity : folio->entities())
            all.insert(entity->id());
    }
    setSelection(all);
}

void FolioView::clearSelection() { setSelection({}); }

// --------------------------------------------------------------------------
// Outils

void FolioView::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    cancelPending();
    m_tool = tool;
    if (tool != Tool::Symbol) {
        m_pendingSymbol.clear();
        m_pendingPrototype.reset();
    }
    m_shapePoints.clear();
    // Le bus arme appartient a l'outil Fil : le laisser survivre a un
    // changement d'outil ferait tracer trois conducteurs a qui n'en demandait
    // qu'un, et l'erreur ne se voit qu'a la netlist.
    if (tool != Tool::Wire)
        m_bus.reset();
    setCursor(Qt::BlankCursor);
    Q_EMIT toolChanged(tool);
    // Revenir a l'outil Symbole les mains vides reprend le dernier pose :
    // c'est l'INSERT d'AutoCAD, et cela evite l'aller-retour vers la palette
    // apres chaque texte intercale dans une serie.
    if (tool == Tool::Symbol && m_pendingSymbol.isEmpty())
        rearmLastSymbol();
    update();
}

void FolioView::setBus(const BusSpec &spec)
{
    if (!spec.isValid()) {
        m_bus.reset();
        update();
        return;
    }
    setTool(Tool::Wire);
    m_bus = spec;
    Q_EMIT statusMessage(tr("Fil multiple : %n conducteur(s) au pas de %1 mm — tracez "
                            "comme un fil.", "", spec.count)
                                 .arg(spec.spacing, 0, 'f', 1));
    update();
}

bool FolioView::rearmLastSymbol()
{
    if (m_lastSymbol.isEmpty()
        || !m_document->project().library.definition(m_lastSymbol)) {
        return false;
    }
    const Orientation orientation = m_lastOrientation;
    setPendingSymbol(m_lastSymbol);
    // setPendingSymbol remet le placement a plat : on repose l'orientation
    // apres, sinon le rearmement perdrait les quarts de tour deja faits.
    m_pendingPlacement.orientation = orientation;
    update();
    return true;
}

void FolioView::setPendingSymbol(const QString &definitionId, const SymbolInstance *prototype)
{
    m_pendingSymbol = definitionId;
    m_pendingPlacement = Placement();
    if (prototype)
        m_pendingPrototype = *prototype;
    else
        m_pendingPrototype.reset();
    if (!definitionId.isEmpty())
        setTool(Tool::Symbol);
    update();
}

bool FolioView::abandonGesture(bool includeSelection)
{
    // L'ordre est celui de l'imbrication : la cote appartient au geste, le
    // geste appartient a l'outil. On ne defait qu'une couche a la fois, pour
    // qu'une frappe de trop ne rende pas l'outil par-dessus le marche.
    if (m_textEntry) {
        cancelTextEntry();
        Q_EMIT statusMessage(tr("Texte abandonné."));
        return true;
    }
    if (m_typing) {
        cancelTyping();
        Q_EMIT statusMessage(tr("Saisie abandonnée."));
        return true;
    }
    if (m_zoomWindowArmed) {
        m_zoomWindowArmed = false;
        m_rubber = QRectF();
        setCursor(Qt::BlankCursor);
        Q_EMIT statusMessage(tr("Zoom fenêtre abandonné."));
        update();
        return true;
    }
    if (m_pending != Pending::None || m_stretchArmed) {
        // Abandonner une designation rend la main sans rien faire : la suite
        // prevue est jetee, sinon elle se declencherait au geste suivant.
        const bool wasPicking = m_pending == Pending::PickObjects;
        m_pickThen = nullptr;
        m_pickPrompt.clear();
        m_pending = Pending::None;
        m_stretchArmed = false;
        m_stretchWindow = QRectF();
        m_rubber = QRectF();
        setCursor(Qt::BlankCursor);
        Q_EMIT statusMessage(wasPicking ? tr("Désignation abandonnée.")
                                        : tr("Geste annulé."));
        update();
        return true;
    }
    if (!m_wirePoints.isEmpty() || !m_shapePoints.isEmpty() || !m_measurePoints.isEmpty()) {
        cancelPending();
        Q_EMIT statusMessage(tr("Tracé abandonné."));
        return true;
    }
    if (m_panArmed) {
        cancelPending();
        Q_EMIT statusMessage(tr("Panoramique terminé."));
        return true;
    }
    if (m_tool != Tool::Select) {
        setTool(Tool::Select);
        Q_EMIT statusMessage(tr("Outil relâché."));
        return true;
    }
    if (includeSelection && !m_selection.isEmpty()) {
        clearSelection();
        return true;
    }
    return false;
}

void FolioView::cancelPending()
{
    m_measurePoints.clear();
    m_shapePoints.clear();
    if (m_panArmed) {
        m_panArmed = false;
        setCursor(Qt::BlankCursor);
    }
    // Les reperes acquis appartiennent a la commande en cours : les garder
    // ferait suivre des alignements sans rapport avec le geste suivant.
    m_snapEngine.clearTracked();
    m_trackHit.reset();
    m_hoverCandidate.reset();
    if (m_acquireTimer)
        m_acquireTimer->stop();
    m_wirePoints.clear();
    m_drag = Drag::None;
    m_rubber = QRectF();
    update();
}

// --------------------------------------------------------------------------
// Style et zoom

void FolioView::setStyle(const RenderStyle &style)
{
    m_style = style;
    update();
}

void FolioView::setGridStep(double step)
{
    if (step <= 0.0)
        return;
    m_style.gridStep = step;
    m_snapEngine.setGridStep(step);
    update();
}

void FolioView::setGridVisible(bool visible)
{
    m_style.showGrid = visible;
    update();
}

void FolioView::setZoom(double pixelsPerMm, const QPointF &anchorPx)
{
    const double target = std::clamp(pixelsPerMm, kMinScale, kMaxScale);
    if (fuzzyEqual(target, m_scale, 1e-9))
        return;
    // Le point vise reste sous le curseur : c'est ce qui rend le zoom a la
    // molette utilisable sur un grand folio.
    const QPointF anchor = anchorPx.isNull() ? QPointF(width() / 2.0, height() / 2.0) : anchorPx;
    const QPointF sceneAnchor = toScene(anchor);
    pushViewState();
    m_scale = target;
    m_pan = anchor - sceneAnchor * m_scale;
    Q_EMIT zoomChanged(m_scale);
    update();
}

void FolioView::pushViewState()
{
    // Une vingtaine de vues suffit : au-dela, personne ne remonte le fil.
    m_viewHistory.append({ m_scale, m_pan });
    if (m_viewHistory.size() > 20)
        m_viewHistory.removeFirst();
}

void FolioView::beginZoomWindow()
{
    m_zoomWindowArmed = true;
    setCursor(Qt::CrossCursor);
    update();
}

void FolioView::zoomToRect(const QRectF &sceneRect)
{
    const QRectF target = sceneRect.normalized();
    if (target.width() < 1e-6 || target.height() < 1e-6 || width() < 20 || height() < 20)
        return;

    pushViewState();
    m_scale = std::clamp(std::min(width() / target.width(), height() / target.height()),
                         kMinScale, kMaxScale);
    m_pan = QPointF(width() / 2.0, height() / 2.0) - target.center() * m_scale;
    m_fitPending = false;
    Q_EMIT zoomChanged(m_scale);
    update();
}

void FolioView::zoomPrevious()
{
    if (m_viewHistory.isEmpty()) {
        Q_EMIT statusMessage(tr("Aucune vue précédente."));
        return;
    }
    const ViewState state = m_viewHistory.takeLast();
    m_scale = state.scale;
    m_pan = state.pan;
    m_fitPending = false;
    Q_EMIT zoomChanged(m_scale);
    update();
}

void FolioView::zoomIn() { setZoom(m_scale * 1.25, mapFromGlobal(QCursor::pos())); }

void FolioView::zoomOut() { setZoom(m_scale / 1.25, mapFromGlobal(QCursor::pos())); }


void FolioView::zoomToFit()
{
    const Folio *folio = m_document->currentFolio();
    if (!folio || width() < 50 || height() < 50) {
        m_fitPending = true;
        return;
    }
    m_fitPending = false;
    pushViewState();
    const QRectF sheet = folio->sheetRect().adjusted(-8, -8, 8, 8);
    const double sx = width() / sheet.width();
    const double sy = height() / sheet.height();
    m_scale = std::clamp(std::min(sx, sy), kMinScale, kMaxScale);
    m_pan = QPointF(width() / 2.0, height() / 2.0) - sheet.center() * m_scale;
    Q_EMIT zoomChanged(m_scale);
    update();
}

// --------------------------------------------------------------------------
// Modifications

void FolioView::deleteSelection()
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_selection.isEmpty())
        return;
    const QStringList ids(m_selection.cbegin(), m_selection.cend());
    // Poser un appareil de passage sur un fil le coupe et le rebranche ;
    // l'enlever doit refermer le trace. Sans cela, effacer un contact laisse
    // deux fils qui pointent vers du vide : le dessin parait juste et le
    // circuit est ouvert. La recouture est calculee AVANT la suppression, sur
    // la geometrie encore en place, et elle entre dans la meme macro — une
    // seule annulation defait le tout.
    int recousus = 0;
    m_document->pushMacro(tr("Supprimer %n élément(s)", "", int(ids.size())), [&] {
        for (const QString &id : ids) {
            const auto *symbol = dynamic_cast<const SymbolInstance *>(folio->entity(id));
            if (!symbol)
                continue;
            const auto heal = ComponentTools::healOnRemoval(*folio, m_document->project().library,
                                                            *symbol, m_selection);
            if (!heal)
                continue;
            const auto *keep = dynamic_cast<const Wire *>(folio->entity(heal->keepWireId));
            if (!keep)
                continue;
            auto after = std::make_unique<Wire>(*keep);
            after->points = heal->points;
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), keep->clone(), std::move(after),
                    tr("Refermer le fil")));
            m_document->push(std::make_unique<RemoveEntityCommand>(
                    m_document->project(), folio->id(), heal->removeWireId,
                    tr("Refermer le fil")));
            ++recousus;
        }
        for (const QString &id : ids)
            m_document->push(std::make_unique<RemoveEntityCommand>(m_document->project(),
                                                                   folio->id(), id));
    });
    if (recousus > 0)
        Q_EMIT statusMessage(tr("%n fil(s) refermé(s) sur l'appareil retiré.", "", recousus));
    clearSelection();
}

int FolioView::swapSymbol(const QString &entityId, const QString &newDefinitionId)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return 0;
    const auto *symbol = dynamic_cast<const SymbolInstance *>(folio->entity(entityId));
    if (!symbol || symbol->definitionId == newDefinitionId)
        return 0;
    const SymbolLibrary &library = m_document->project().library;
    const SymbolDefinition *neuf = library.definition(newDefinitionId);
    if (!neuf)
        return 0;

    // Le plan est calcule AVANT toute modification, sur la geometrie en
    // place : une fois la definition changee, les anciennes broches n'existent
    // plus et on ne saurait plus ou les fils tenaient.
    const auto plan = ComponentTools::planSwap(*folio, library, *symbol, newDefinitionId);

    m_document->pushMacro(tr("Remplacer le symbole"), [&] {
        auto apres = std::make_unique<SymbolInstance>(*symbol);
        apres->definitionId = newDefinitionId;
        apres->setLocalBounds(neuf->bounds());
        // Le repere et les champs ne sont PAS touches : c'est tout l'interet
        // du geste, et l'invariant « ce que l'utilisateur a saisi n'est jamais
        // ecrase » l'exige. Un contact qui devient un contact NF reste -K1.
        m_document->push(std::make_unique<ModifyEntityCommand>(
                m_document->project(), folio->id(), symbol->clone(), std::move(apres),
                tr("Remplacer le symbole")));

        for (const auto &move : plan.moves) {
            const auto *wire = dynamic_cast<const Wire *>(folio->entity(move.wireId));
            if (!wire || move.vertex >= wire->points.size())
                continue;
            auto apresFil = std::make_unique<Wire>(*wire);
            apresFil->points[move.vertex] = move.to;
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), wire->clone(), std::move(apresFil),
                    tr("Suivre la broche")));
        }
    });

    // Pas de componentPlaced ici : ce signal ouvre la boite du composant quand
    // le reglage l'exige, et remplacer un symbole n'est pas une pose.
    update();
    return plan.orphaned;
}

void FolioView::rotateSelection(bool clockwise)
{
    if (m_tool == Tool::Symbol && !m_pendingSymbol.isEmpty()) {
        // Le symbole arme tourne avant d'etre pose : c'est le geste attendu.
        m_pendingPlacement.orientation = clockwise ? rotateCw(m_pendingPlacement.orientation)
                                                   : rotateCcw(m_pendingPlacement.orientation);
        update();
        return;
    }

    Folio *folio = m_document->currentFolio();
    if (!folio || m_selection.isEmpty())
        return;

    m_document->pushMacro(tr("Pivoter la sélection"), [&] {
        for (const QString &id : std::as_const(m_selection)) {
            Entity *entity = folio->entity(id);
            auto *symbol = dynamic_cast<SymbolInstance *>(entity);
            if (!symbol)
                continue;
            auto before = symbol->clone();
            auto after = symbol->clone();
            auto *typed = static_cast<SymbolInstance *>(after.get());
            typed->placement.orientation = clockwise ? rotateCw(typed->placement.orientation)
                                                     : rotateCcw(typed->placement.orientation);
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), std::move(before), std::move(after),
                    tr("Pivoter")));
        }
    });
}

void FolioView::mirrorSelection()
{
    if (m_tool == Tool::Symbol && !m_pendingSymbol.isEmpty()) {
        m_pendingPlacement.mirrored = !m_pendingPlacement.mirrored;
        update();
        return;
    }

    Folio *folio = m_document->currentFolio();
    if (!folio || m_selection.isEmpty())
        return;

    m_document->pushMacro(tr("Retourner la sélection"), [&] {
        for (const QString &id : std::as_const(m_selection)) {
            auto *symbol = dynamic_cast<SymbolInstance *>(folio->entity(id));
            if (!symbol)
                continue;
            auto before = symbol->clone();
            auto after = symbol->clone();
            auto *typed = static_cast<SymbolInstance *>(after.get());
            typed->placement.mirrored = !typed->placement.mirrored;
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), std::move(before), std::move(after),
                    tr("Retourner")));
        }
    });
}

void FolioView::nudgeSelection(const QPointF &deltaMm)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_selection.isEmpty())
        return;
    const QStringList ids(m_selection.cbegin(), m_selection.cend());
    m_document->push(std::make_unique<MoveEntitiesCommand>(m_document->project(), folio->id(),
                                                           ids, deltaMm));
}

// --------------------------------------------------------------------------
// La designation a la demande — le « Select objects: » d'AutoCAD.

bool FolioView::matchesFilter(const Entity *entity, PickFilter filter) const
{
    if (!entity)
        return false;
    switch (filter) {
    case PickFilter::Any: return true;
    case PickFilter::Wires: return dynamic_cast<const Wire *>(entity) != nullptr;
    case PickFilter::Symbols: return dynamic_cast<const SymbolInstance *>(entity) != nullptr;
    }
    return false;
}

bool FolioView::selectionSatisfies(PickFilter filter, int minimum) const
{
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return false;
    int count = 0;
    for (const QString &id : m_selection) {
        if (matchesFilter(folio->entity(id), filter))
            ++count;
    }
    return count >= minimum;
}

void FolioView::requestSelection(const QString &prompt, PickFilter filter, int minimum,
                                 std::function<void()> then)
{
    // La designation part d'une ardoise vide : garder une selection qui ne
    // convient pas ferait valider un lot dont une partie n'a rien a faire la.
    setTool(Tool::Select);
    if (!selectionSatisfies(filter, minimum))
        clearSelection();

    m_pending = Pending::PickObjects;
    m_pickPrompt = prompt;
    m_pickFilter = filter;
    m_pickMinimum = std::max(1, minimum);
    m_pickThen = std::move(then);
    setCursor(Qt::CrossCursor);
    update();
}

void FolioView::finishPick()
{
    if (m_pending != Pending::PickObjects)
        return;
    if (!selectionSatisfies(m_pickFilter, m_pickMinimum)) {
        Q_EMIT statusMessage(m_pickMinimum > 1
                                     ? tr("Il en faut au moins %n. Échap pour abandonner.", "",
                                          m_pickMinimum)
                                     : tr("Rien de désigné. Échap pour abandonner."));
        return;
    }
    // La suite est prise puis effacee AVANT d'etre appelee : elle rappelle la
    // commande, qui pourrait redemander une designation, et une continuation
    // encore en place serait ecrasee au milieu de son propre appel.
    const auto then = m_pickThen;
    m_pickThen = nullptr;
    m_pending = Pending::None;
    m_pickPrompt.clear();
    setCursor(Qt::BlankCursor);
    update();
    if (then)
        then();
}

// --------------------------------------------------------------------------
// L'invite : ce que la commande en cours attend
//
// Elle sort de l'etat, elle n'y est pas poussee. Voir folioview.h pour le
// pourquoi. L'ordre de lecture compte : ce qui est le plus imperatif passe
// devant. La saisie de cote couvre tout — tant qu'un chiffre est en train
// d'etre frappe, c'est lui que le logiciel attend, quel que soit l'outil.

QString FolioView::currentPrompt() const
{
    if (m_textEntry) {
        return m_textIsLabel
                ? tr("Nom du potentiel : tapez, Entrée valide, Échap annule.")
                : tr("Texte (%1 mm) : tapez, Entrée valide, Échap annule.")
                          .arg(m_textHeight, 0, 'g', 2);
    }
    if (m_typing) {
        return tr("Cote : %1▏ — Entrée valide, Échap abandonne. "
                  "Formes : 50, 50<45, @10,5, #120,80")
                .arg(m_typed);
    }

    switch (m_pending) {
    case Pending::PickObjects: {
        const int chosen = int(m_selection.size());
        if (chosen == 0)
            return tr("%1 — Échap pour abandonner.").arg(m_pickPrompt);
        if (chosen < m_pickMinimum)
            return tr("%1 — %n désigné(s), il en faut %2.", "", chosen)
                    .arg(m_pickPrompt)
                    .arg(m_pickMinimum);
        return tr("%1 — %n désigné(s), Entrée pour valider.", "", chosen).arg(m_pickPrompt);
    }
    case Pending::MoveBase:
        return tr("Déplacer : cliquer le point de base.");
    case Pending::MoveTarget:
        return tr("Déplacer : cliquer le point d'arrivée, ou taper la cote.");
    case Pending::OffsetSide:
        return tr("Décaler de %1 mm : cliquer du côté voulu.")
                .arg(m_offsetDistance, 0, 'f', 1);
    case Pending::StretchBase:
        return tr("Étirer : cliquer le point de base.");
    case Pending::StretchTarget:
        return tr("Étirer : cliquer le point d'arrivée, ou taper la cote.");
    case Pending::ScootTarget:
        return tr("Glisser le long du fil : cliquer la nouvelle position.");
    case Pending::ComponentTarget:
        return tr("Déplacer l'appareil : cliquer la nouvelle position, "
                  "les fils raccordés suivent.");
    case Pending::ScaleBase:
        return tr("Échelle : cliquer le point fixe, celui qui ne bougera pas.");
    case Pending::ScaleTarget:
        return tr("Échelle : éloigner pour grossir, ou taper le facteur (×%1).")
                .arg(m_scaleFactor, 0, 'g', 3);
    case Pending::CutTarget:
        return tr("Coupure : cliquer l'endroit où couper le fil.");
    case Pending::MeasureDistance:
        return m_measurePoints.isEmpty() ? tr("Mesurer : cliquer le premier point.")
                                         : tr("Mesurer : cliquer le second point.");
    case Pending::None:
        break;
    }

    // Les modes armes par une commande de vue. Ils ne sont pas des Pending
    // parce qu'ils se resolvent par un glisser, pas par un clic.
    if (m_zoomWindowArmed)
        return tr("Zoom fenêtre : encadrer la zone à agrandir.");
    if (m_stretchArmed)
        return tr("Étirer : encadrer les sommets à déplacer (fenêtre de capture).");
    if (m_panArmed)
        return tr("Panoramique : tirer pour déplacer la vue.");

    // L'outil modal. Un trace commence n'attend pas la meme chose qu'un trace
    // a poser : c'est la difference entre « ou ? » et « et ensuite ? ».
    switch (m_tool) {
    case Tool::Select:
        return QString();
    case Tool::Wire:
        if (m_wirePoints.isEmpty()) {
            return m_bus ? tr("Fil multiple (%n conducteurs) : cliquer le départ.", "",
                              m_bus->count)
                         : tr("Fil : cliquer le départ.");
        }
        return tr("Fil : cliquer un coude, ou taper la cote — "
                  "Entrée termine, Échap annule.");
    case Tool::Symbol: {
        const SymbolDefinition *definition =
                m_document->project().library.definition(m_pendingSymbol);
        if (definition) {
            // Les deux touches du geste sont dans l'invite, pas dans une
            // info-bulle : on ne survole pas un bouton pendant qu'on pose.
            return tr("Insérer « %1 » : cliquer l'emplacement — R pivote, M retourne, "
                      "Échap annule.")
                    .arg(definition->name);
        }
        return tr("Insérer : choisir un symbole dans la palette.");
    }
    case Tool::Junction:
        return tr("Nœud : cliquer le point de raccordement.");
    case Tool::Label:
        return tr("Étiquette : cliquer l'emplacement du texte.");
    case Tool::Text:
        return tr("Texte : cliquer l'emplacement.");
    case Tool::Trim:
        return tr("Ajuster : viser la portion de fil à retirer.");
    case Tool::Extend:
        return tr("Prolonger : viser le fil, près de l'extrémité à allonger.");
    case Tool::Line:
        return m_shapePoints.isEmpty() ? tr("Ligne : cliquer le premier point.")
                                       : tr("Ligne : cliquer le second point.");
    case Tool::Rectangle:
        return m_shapePoints.isEmpty() ? tr("Rectangle : cliquer un coin.")
                                       : tr("Rectangle : cliquer le coin opposé.");
    case Tool::Circle:
        return m_shapePoints.isEmpty()
                       ? tr("Cercle : cliquer le centre.")
                       : tr("Cercle : cliquer un point du contour, ou taper le rayon.");
    case Tool::Arc:
        if (m_shapePoints.isEmpty())
            return tr("Arc : cliquer le point de départ.");
        return m_shapePoints.size() == 1 ? tr("Arc : cliquer un point par lequel il passe.")
                                         : tr("Arc : cliquer le point d'arrivée.");
    case Tool::Polyline:
        if (m_shapePoints.isEmpty())
            return tr("Polyligne : cliquer le premier sommet.");
        return tr("Polyligne : %n sommet(s) — Entrée termine, Retour arrière défait, "
                  "Échap annule.", "", int(m_shapePoints.size()));
    }
    return QString();
}

void FolioView::refreshPrompt()
{
    const QString prompt = currentPrompt();
    if (prompt == m_lastPrompt)
        return;
    m_lastPrompt = prompt;
    Q_EMIT promptChanged(prompt);
}

void FolioView::beginMoveSelection()
{
    if (!selectionSatisfies(PickFilter::Any, 1)) {
        requestSelection(tr("Déplacer : désignez ce qu'il faut déplacer"), PickFilter::Any, 1,
                         [this] { beginMoveSelection(); });
        return;
    }
    setTool(Tool::Select);
    m_pending = Pending::MoveBase;
    update();
}

void FolioView::beginOffset(double distanceMm)
{
    if (distanceMm <= 0.0)
        return;
    if (!selectionSatisfies(PickFilter::Wires, 1)) {
        requestSelection(tr("Décaler : désignez le fil à décaler"), PickFilter::Wires, 1,
                         [this, distanceMm] { beginOffset(distanceMm); });
        return;
    }
    setTool(Tool::Select);
    m_offsetDistance = distanceMm;
    m_pending = Pending::OffsetSide;
    update();
}

void FolioView::beginScale()
{
    if (!selectionSatisfies(PickFilter::Any, 1)) {
        requestSelection(tr("Échelle : désignez ce qu'il faut grossir ou réduire"),
                         PickFilter::Any, 1, [this] { beginScale(); });
        return;
    }
    setTool(Tool::Select);
    m_pending = Pending::ScaleBase;
    m_scaleFactor = 1.0;
    update();
}

void FolioView::beginCut()
{
    if (!m_document->currentFolio())
        return;
    if (!selectionSatisfies(PickFilter::Wires, 1)) {
        requestSelection(tr("Coupure : désignez le fil à couper"), PickFilter::Wires, 1,
                         [this] { beginCut(); });
        return;
    }
    setTool(Tool::Select);
    m_pending = Pending::CutTarget;
    update();
}

void FolioView::joinSelectedWires()
{
    Folio *folio = m_document->currentFolio();
    if (!folio) {
        return;
    }
    QStringList wires;
    for (const QString &id : std::as_const(m_selection)) {
        if (dynamic_cast<const Wire *>(folio->entity(id)))
            wires.append(id);
    }
    if (wires.size() < 2) {
        Q_EMIT statusMessage(tr("Joindre : sélectionner au moins deux fils qui se touchent."));
        return;
    }

    // On soude de proche en proche : chaque soudure peut en rendre une autre
    // possible, et l'utilisateur attend qu'une selection de quatre morceaux
    // n'en laisse qu'un.
    int joined = 0;
    m_document->pushMacro(tr("Joindre les fils"), [&] {
        bool again = true;
        while (again) {
            again = false;
            for (int i = 0; i < wires.size() && !again; ++i) {
                for (int j = i + 1; j < wires.size() && !again; ++j) {
                    const auto join = EditTools::joinable(*folio, wires.at(i), wires.at(j));
                    if (!join)
                        continue;
                    const auto *pattern = dynamic_cast<const Wire *>(folio->entity(join->firstId));
                    if (!pattern)
                        continue;
                    // Le fil soude herite du premier : repere, conducteurs et
                    // type. Souder ne doit pas faire perdre son identite au fil.
                    auto merged = std::make_unique<Wire>(*pattern);
                    merged->setId(newId());
                    merged->points = join->merged;
                    const QString mergedId = merged->id();

                    m_document->push(std::make_unique<RemoveEntityCommand>(
                            m_document->project(), folio->id(), join->firstId,
                            tr("Joindre les fils")));
                    m_document->push(std::make_unique<RemoveEntityCommand>(
                            m_document->project(), folio->id(), join->secondId,
                            tr("Joindre les fils")));
                    m_document->push(std::make_unique<AddEntityCommand>(
                            m_document->project(), folio->id(), std::move(merged),
                            tr("Joindre les fils")));

                    wires.removeAt(j);
                    wires.removeAt(i);
                    wires.append(mergedId);
                    ++joined;
                    again = true;
                }
            }
        }
    });

    if (joined == 0) {
        Q_EMIT statusMessage(tr("Joindre : ces fils ne se touchent pas, ou n'ont pas le même "
                                "type."));
        return;
    }
    setSelection(QSet<QString>{ wires.last() });
    Q_EMIT statusMessage(tr("%n soudure(s).", "", joined));
}

namespace {

// Centre du cercle circonscrit a trois points. Sans lui, pas d'arc par trois
// points — et c'est la facon dont on trace un arc quand on sait ou il part,
// par ou il passe et ou il arrive, ce qui est le cas sur un plan.
std::optional<QPointF> circumcenter(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const double d = 2.0 * (a.x() * (b.y() - c.y()) + b.x() * (c.y() - a.y())
                            + c.x() * (a.y() - b.y()));
    if (std::abs(d) < 1e-9)
        return std::nullopt; // trois points alignes : aucun cercle ne passe par eux
    const double aa = a.x() * a.x() + a.y() * a.y();
    const double bb = b.x() * b.x() + b.y() * b.y();
    const double cc = c.x() * c.x() + c.y() * c.y();
    return QPointF((aa * (b.y() - c.y()) + bb * (c.y() - a.y()) + cc * (a.y() - b.y())) / d,
                   (aa * (c.x() - b.x()) + bb * (a.x() - c.x()) + cc * (b.x() - a.x())) / d);
}

// Angle d'un point vu du centre, dans la convention de Primitive : degres,
// sens trigonometrique visuel, origine a trois heures. L'axe y de l'ecran
// descend, d'ou le signe.
double angleFrom(const QPointF &center, const QPointF &p)
{
    return std::atan2(-(p.y() - center.y()), p.x() - center.x()) * 180.0 / std::numbers::pi;
}

double normalizedAngle(double degrees)
{
    double a = std::fmod(degrees, 360.0);
    if (a < 0.0)
        a += 360.0;
    return a;
}

} // namespace

std::optional<Primitive> FolioView::pendingShape(const QPointF &cursor) const
{
    if (m_shapePoints.isEmpty())
        return std::nullopt;
    const QPointF first = m_shapePoints.first();

    switch (m_tool) {
    case Tool::Line: {
        if (samePoint(first, cursor))
            return std::nullopt;
        return Primitive::line(first, cursor);
    }
    case Tool::Rectangle: {
        const QRectF r = normalized(first, cursor);
        if (r.width() < kConnectTolerance || r.height() < kConnectTolerance)
            return std::nullopt;
        return Primitive::rect(r);
    }
    case Tool::Circle: {
        const double radius = std::hypot(cursor.x() - first.x(), cursor.y() - first.y());
        if (radius < kConnectTolerance)
            return std::nullopt;
        Primitive p;
        p.kind = Primitive::Kind::Circle;
        p.points = { first };
        p.radius = radius;
        return p;
    }
    case Tool::Arc: {
        if (m_shapePoints.size() < 2)
            return std::nullopt;
        const QPointF through = m_shapePoints.at(1);
        const QPointF end = m_shapePoints.size() >= 3 ? m_shapePoints.at(2) : cursor;
        const auto center = circumcenter(first, through, end);
        if (!center)
            return std::nullopt;

        Primitive p;
        p.kind = Primitive::Kind::Arc;
        p.points = { *center };
        p.radius = std::hypot(first.x() - center->x(), first.y() - center->y());
        p.startAngle = angleFrom(*center, first);
        // Le sens est celui qui passe par le point du milieu. Sans ce choix,
        // un arc sur deux part du mauvais cote du cercle.
        const double toMiddle = normalizedAngle(angleFrom(*center, through) - p.startAngle);
        const double toEnd = normalizedAngle(angleFrom(*center, end) - p.startAngle);
        p.spanAngle = toMiddle <= toEnd ? toEnd : toEnd - 360.0;
        return p;
    }
    case Tool::Polyline: {
        QVector<QPointF> points = m_shapePoints;
        if (!samePoint(points.last(), cursor))
            points.append(cursor);
        if (points.size() < 2)
            return std::nullopt;
        return Primitive::polyline(points);
    }
    default:
        return std::nullopt;
    }
}

void FolioView::placeShapePoint(const QPointF &point)
{
    // Le nombre de points attendus depend de l'outil : deux pour un
    // rectangle ou un cercle, trois pour un arc, autant qu'on veut pour une
    // polyligne.
    const int wanted = m_tool == Tool::Arc ? 3
            : m_tool == Tool::Polyline     ? -1
                                           : 2;

    if (!m_shapePoints.isEmpty() && samePoint(point, m_shapePoints.last()))
        return;
    m_shapePoints.append(point);

    if (wanted > 0 && m_shapePoints.size() >= wanted) {
        commitShape();
        return;
    }

    update();
}

void FolioView::commitShape()
{
    Folio *folio = m_document->currentFolio();
    // La forme se construit sur les points deja poses, jamais sur le curseur
    // re-accroche : au moment de valider, le dernier point est dans la liste,
    // et le re-accrocher le ferait passer une seconde fois par la contrainte
    // de direction — avec pour origine le point qu'il vient de produire. Un
    // trait tire a l'horizontale finissait ainsi par un coude vertical.
    const QPointF cursor = m_shapePoints.last();
    const auto shape = pendingShape(cursor);
    if (!folio || !shape) {
        // Un arc dont les trois points sont alignes n'existe pas : le dire
        // vaut mieux que de poser un segment a la place et laisser croire que
        // c'est un arc.
        if (m_tool == Tool::Arc && m_shapePoints.size() >= 3)
            Q_EMIT statusMessage(tr("Ces trois points sont alignés : aucun arc ne passe "
                                    "par eux."));
        m_shapePoints.clear();
        update();
        return;
    }

    auto item = std::make_unique<GraphicItem>();
    item->shape = *shape;
    item->shape.stroke = m_shapeStroke;
    m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                        std::move(item), tr("Dessiner")));
    m_shapePoints.clear();
    // L'outil reste arme : on trace rarement un seul encadre.
    Q_EMIT statusMessage(tr("Forme posée."));
    update();
}

void FolioView::paintShapePreview(QPainter &painter) const
{
    if (m_shapePoints.isEmpty())
        return;
    const auto shape = pendingShape(m_cursorInside ? snap(m_cursorMm) : m_shapePoints.last());
    if (!shape)
        return;

    painter.save();
    QPen pen(m_style.selection);
    pen.setWidthF(m_style.wireWidth);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (shape->kind) {
    case Primitive::Kind::Line:
        if (shape->points.size() >= 2)
            painter.drawLine(shape->points.first(), shape->points.last());
        break;
    case Primitive::Kind::Polyline:
        painter.drawPolyline(shape->points.constData(), int(shape->points.size()));
        break;
    case Primitive::Kind::Rect:
        if (shape->points.size() >= 2)
            painter.drawRect(QRectF(shape->points.first(), shape->points.last()));
        break;
    case Primitive::Kind::Circle:
        painter.drawEllipse(shape->points.first(), shape->radius, shape->radius);
        break;
    case Primitive::Kind::Arc: {
        const QPointF c = shape->points.first();
        const QRectF box(c.x() - shape->radius, c.y() - shape->radius, shape->radius * 2.0,
                         shape->radius * 2.0);
        painter.drawArc(box, int(shape->startAngle * 16), int(shape->spanAngle * 16));
        break;
    }
    case Primitive::Kind::Text:
        break;
    }
    painter.restore();
}

void FolioView::beginPan()
{
    setTool(Tool::Select);
    m_panArmed = true;
    setCursor(Qt::OpenHandCursor);
    update();
}

void FolioView::beginMeasureDistance()
{
    setTool(Tool::Select);
    m_measurePoints.clear();
    m_pending = Pending::MeasureDistance;
    update();
}

QString FolioView::measureReport() const
{
    if (m_measurePoints.size() < 2)
        return QString();

    if (m_pending == Pending::MeasureDistance || m_measurePoints.size() == 2) {
        const QPointF a = m_measurePoints.first();
        const QPointF b = m_measurePoints.last();
        const QPointF d = b - a;
        const double length = std::hypot(d.x(), d.y());
        // L'angle est donne dans la convention du dessinateur : zero a
        // l'horizontale, positif vers le haut. L'axe y de l'ecran descend,
        // d'ou le signe.
        double angle = std::atan2(-d.y(), d.x()) * 180.0 / std::numbers::pi;
        if (angle < 0.0)
            angle += 360.0;
        return tr("Distance %1 mm   ΔX %2   ΔY %3   angle %4°")
                .arg(length, 0, 'f', 2)
                .arg(d.x(), 0, 'f', 2)
                .arg(-d.y(), 0, 'f', 2)
                .arg(angle, 0, 'f', 1);
    }

    // Surface d'un polygone quelconque par la formule du lacet. La valeur
    // absolue rend le sens de saisie sans importance : on mesure une surface,
    // pas une orientation.
    double twice = 0.0;
    double perimeter = 0.0;
    for (int i = 0; i < m_measurePoints.size(); ++i) {
        const QPointF &a = m_measurePoints.at(i);
        const QPointF &b = m_measurePoints.at((i + 1) % m_measurePoints.size());
        twice += a.x() * b.y() - b.x() * a.y();
        perimeter += std::hypot(b.x() - a.x(), b.y() - a.y());
    }
    const double area = std::abs(twice) / 2.0;
    return tr("Surface %1 mm² (%2 m²)   périmètre %3 mm   %n sommet(s)", "",
              int(m_measurePoints.size()))
            .arg(area, 0, 'f', 1)
            .arg(area / 1e6, 0, 'f', 4)
            .arg(perimeter, 0, 'f', 1);
}

void FolioView::paintMeasure(QPainter &painter) const
{
    if (m_measurePoints.isEmpty())
        return;

    QVector<QPointF> path = m_measurePoints;
    if (m_cursorInside)
        path.append(snap(m_cursorMm));

    painter.save();
    QPen pen(m_style.snapGuide);
    pen.setWidthF(m_style.wireWidth);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(path.constData(), int(path.size()));

    QPen marks(m_style.snapMarker);
    marks.setWidthF(m_style.wireWidth * 1.2);
    painter.setPen(marks);
    painter.setBrush(Qt::NoBrush);
    for (const QPointF &p : m_measurePoints) {
        const double r = 1.4;
        painter.drawLine(p + QPointF(-r, -r), p + QPointF(r, r));
        painter.drawLine(p + QPointF(-r, r), p + QPointF(r, -r));
    }
    painter.restore();
}

void FolioView::applyScale(double factor)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    auto command = std::make_unique<ScaleEntitiesCommand>(
            m_document->project(), folio->id(), QStringList(m_selection.cbegin(),
                                                            m_selection.cend()),
            m_moveBase, factor);
    if (command->affectedCount() == 0) {
        Q_EMIT statusMessage(tr("Échelle : rien à grossir."));
        return;
    }
    m_document->push(std::move(command));
    rebuildGrips();
    Q_EMIT statusMessage(tr("Échelle appliquée : ×%1.").arg(factor, 0, 'g', 3));
}

SymbolInstance *FolioView::selectedComponent() const
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return nullptr;
    for (const QString &id : m_selection) {
        if (auto *symbol = dynamic_cast<SymbolInstance *>(folio->entity(id)))
            return symbol;
    }
    return nullptr;
}

void FolioView::beginScoot()
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    SymbolInstance *symbol = selectedComponent();
    if (!symbol) {
        requestSelection(tr("Glisser : désignez l'appareil à faire coulisser"),
                         PickFilter::Symbols, 1, [this] { beginScoot(); });
        return;
    }

    m_scootAxis = ComponentTools::scootAxis(*folio, m_document->project().library, *symbol);
    if (!m_scootAxis) {
        // Sans axe, Scoot n'a pas de sens : on le dit plutot que de glisser
        // dans une direction arbitraire.
        Q_EMIT statusMessage(tr("Glisser : cet appareil n'est raccordé à aucun fil, "
                                "ou ses fils tirent dans des directions différentes. "
                                "Utilisez « Déplacer l'appareil »."));
        return;
    }

    setTool(Tool::Select);
    m_componentId = symbol->id();
    m_componentStart = symbol->placement.position;
    m_pending = Pending::ScootTarget;
    update();
}

void FolioView::beginMoveComponent()
{
    SymbolInstance *symbol = selectedComponent();
    if (!symbol) {
        requestSelection(tr("Déplacer l'appareil : désignez l'appareil"), PickFilter::Symbols,
                         1, [this] { beginMoveComponent(); });
        return;
    }
    setTool(Tool::Select);
    m_scootAxis.reset();
    m_componentId = symbol->id();
    m_componentStart = symbol->placement.position;
    m_pending = Pending::ComponentTarget;
    update();
}

void FolioView::beginStretch()
{
    setTool(Tool::Select);
    m_pending = Pending::None;
    m_stretchArmed = true;
    m_stretchWindow = QRectF();
    update();
}

void FolioView::applyStretch(const QPointF &delta)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_stretchWindow.isNull())
        return;

    auto command = std::make_unique<StretchEntitiesCommand>(m_document->project(), folio->id(),
                                                            m_stretchWindow, delta);
    const int count = command->affectedCount();
    if (count == 0) {
        Q_EMIT statusMessage(tr("Étirer : la fenêtre n'a pris aucun sommet."));
        return;
    }
    m_document->push(std::move(command));
    rebuildGrips();
    Q_EMIT statusMessage(tr("%n élément(s) étiré(s).", "", count));
}

bool FolioView::applyPointAt(const QPointF &scenePoint)
{
    // Une cote en cours de frappe est abandonnee des qu'on designe autrement :
    // le clic dit ou, la frappe ne dit plus rien.
    if (handlePendingClick(scenePoint)) {
        cancelTyping();
        return true;
    }
    return false;
}

void FolioView::placeAt(const QPointF &scenePoint)
{
    switch (m_tool) {
    case Tool::Wire:
        if (m_wirePoints.isEmpty()) {
            beginWireAt(scenePoint);
        } else {
            const QPointF previous = m_wirePoints.last();
            if (!samePoint(scenePoint, previous))
                m_wirePoints.append(scenePoint);
        }
        update();
        return;
    case Tool::Symbol:
        // L'outil reste arme : poser dix bornes ne doit pas demander dix
        // allers-retours vers la palette.
        placeSymbolAt(scenePoint);
        return;
    case Tool::Junction:
        placeJunctionAt(scenePoint);
        return;
    case Tool::Label:
        placeLabelAt(scenePoint);
        return;
    case Tool::Text:
        placeTextAt(scenePoint);
        return;
    case Tool::Line:
    case Tool::Rectangle:
    case Tool::Circle:
    case Tool::Arc:
    case Tool::Polyline:
        placeShapePoint(scenePoint);
        return;
    case Tool::Trim:
    case Tool::Extend:
    case Tool::Select:
        break;
    }
}

QPointF FolioView::committedPoint() const
{
    if (m_typing) {
        const QPointF *from = gestureOrigin();
        if (const auto typed = CoordinateEntry::resolve(m_typed, from, snap(m_cursorMm)))
            return *typed;
    }
    return snap(m_cursorMm);
}

void FolioView::cancelTyping()
{
    if (!m_typing)
        return;
    m_typing = false;
    m_typed.clear();
    update();
}

bool FolioView::commitTypedEntry()
{
    if (!m_typing)
        return false;

    // Pendant l'echelle, un nombre tape est un facteur, pas une cote : « 2 »
    // doit doubler, pas deplacer de deux millimetres. C'est le seul moyen
    // d'obtenir exactement 0,5 — la souris ne le donnera jamais.
    if (m_pending == Pending::ScaleTarget) {
        bool ok = false;
        QString text = m_typed.trimmed();
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        const double factor = text.toDouble(&ok);
        m_typing = false;
        m_typed.clear();
        if (!ok || factor <= 0.0) {
            Q_EMIT statusMessage(tr("Facteur incompris : « %1 ». Un nombre positif, "
                                    "par exemple 2 ou 0,5.").arg(text));
            update();
            return false;
        }
        m_pending = Pending::None;
        applyScale(factor);
        update();
        return true;
    }

    const QPointF *from = gestureOrigin();
    const auto point = CoordinateEntry::resolve(m_typed, from, snap(m_cursorMm));
    if (!point) {
        Q_EMIT statusMessage(tr("Saisie incomprise : « %1 ». Formes acceptées : 50, "
                                "50<45, @10,5, #120,80.").arg(m_typed));
        return false;
    }

    m_typing = false;
    m_typed.clear();
    if (!applyPointAt(*point))
        placeAt(*point);
    update();
    return true;
}

bool FolioView::handleTypedKey(QKeyEvent *event)
{
    // La saisie n'a de sens que pendant un geste : hors commande, un chiffre
    // reste libre pour d'autres usages.
    // La cote au clavier vaut pour tout ce qui trace : un cercle se donne
    // aussi bien par son rayon tape que par un clic sur son contour.
    const bool commandRunning = !m_wirePoints.isEmpty() || !m_shapePoints.isEmpty()
            || m_pending != Pending::None || m_tool == Tool::Symbol
            || m_tool == Tool::Junction || m_tool == Tool::Label || m_tool == Tool::Text
            || m_tool == Tool::Wire || m_tool == Tool::Line || m_tool == Tool::Rectangle
            || m_tool == Tool::Circle || m_tool == Tool::Arc || m_tool == Tool::Polyline;

    if (m_typing) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitTypedEntry();
            return true;
        case Qt::Key_Escape:
            // Echap abandonne la cote, pas la commande : on s'est trompe de
            // chiffre, on ne veut pas recommencer le trace.
            cancelTyping();
            Q_EMIT statusMessage(tr("Saisie abandonnée."));
            return true;
        case Qt::Key_Backspace:
            m_typed.chop(1);
            if (m_typed.isEmpty())
                m_typing = false;
            update();
            return true;
        default:
            break;
        }
        if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
            m_typed += event->text();
            update();
            return true;
        }
        return false;
    }

    if (!commandRunning || !CoordinateEntry::startsEntry(event->text()))
        return false;

    m_typing = true;
    m_typed = event->text();
    update();
    return true;
}

bool FolioView::handlePendingClick(const QPointF &scenePoint)
{
    switch (m_pending) {
    case Pending::None:
        return false;
    case Pending::PickObjects: {
        // Un clic ajoute ou retire ; ce qui ne correspond pas au filtre le dit
        // au lieu d'etre ignore. Ignorer en silence, c'est faire croire que le
        // clic a rate alors qu'il a porte sur le mauvais type d'objet.
        Folio *folio = m_document->currentFolio();
        Entity *hit = folio ? entityAt(scenePoint) : nullptr;
        if (!hit) {
            // Rien sous le curseur : on laisse le glisser tracer une fenetre
            // de designation, comme dans l'outil Selection.
            return false;
        }
        if (!matchesFilter(hit, m_pickFilter)) {
            Q_EMIT statusMessage(m_pickFilter == PickFilter::Wires
                                         ? tr("Ce n'est pas un fil. %1").arg(m_pickPrompt)
                                         : tr("Ce n'est pas un appareil. %1").arg(m_pickPrompt));
            return true;
        }
        QSet<QString> chosen = m_selection;
        if (chosen.contains(hit->id()))
            chosen.remove(hit->id());
        else
            chosen.insert(hit->id());
        setSelection(chosen);
        return true;
    }
    case Pending::MoveBase:
        m_moveBase = scenePoint;
        m_pending = Pending::MoveTarget;
        update();
        return true;
    case Pending::MoveTarget: {
        const QPointF delta = scenePoint - m_moveBase;
        m_pending = Pending::None;
        if (!samePoint(delta, QPointF())) {
            nudgeSelection(delta);
            Q_EMIT statusMessage(tr("Déplacé de %1 ; %2 mm.")
                                         .arg(delta.x(), 0, 'f', 2)
                                         .arg(delta.y(), 0, 'f', 2));
        }
        update();
        return true;
    }
    case Pending::OffsetSide:
        applyOffset(scenePoint);
        m_pending = Pending::None;
        update();
        return true;
    case Pending::ScootTarget:
    case Pending::ComponentTarget: {
        Folio *folio = m_document->currentFolio();
        const bool scoot = m_pending == Pending::ScootTarget;
        m_pending = Pending::None;
        if (!folio) {
            update();
            return true;
        }
        QPointF delta = scenePoint - m_componentStart;
        if (scoot && m_scootAxis)
            delta = ComponentTools::constrainToAxis(delta, *m_scootAxis);
        if (!samePoint(delta, QPointF())) {
            auto command = std::make_unique<MoveComponentCommand>(
                    m_document->project(), folio->id(), m_componentId, delta,
                    m_document->project().library);
            const int attached = command->attachedCount();
            m_document->push(std::move(command));
            rebuildGrips();
            Q_EMIT statusMessage(attached == 0
                                         ? tr("Appareil déplacé.")
                                         : tr("Appareil déplacé, %n fil(s) suivi(s).", "",
                                              attached));
        }
        m_scootAxis.reset();
        m_componentId.clear();
        update();
        return true;
    }
    case Pending::ScaleBase: {
        m_moveBase = scenePoint;
        m_pending = Pending::ScaleTarget;
        // Le rayon de reference est la distance du point fixe au coin le plus
        // eloigne de la selection : poser le curseur la donne le facteur 1.
        QRectF hull;
        if (Folio *folio = m_document->currentFolio()) {
            for (const QString &id : std::as_const(m_selection)) {
                if (const Entity *entity = folio->entity(id)) {
                    hull = hull.isNull() ? entity->boundingBox()
                                         : hull.united(entity->boundingBox());
                }
            }
        }
        m_scaleRadius = 0.0;
        for (const QPointF &corner : { hull.topLeft(), hull.topRight(), hull.bottomLeft(),
                                       hull.bottomRight() }) {
            m_scaleRadius = std::max(m_scaleRadius,
                                     std::hypot(corner.x() - scenePoint.x(),
                                                corner.y() - scenePoint.y()));
        }
        if (m_scaleRadius <= kConnectTolerance)
            m_scaleRadius = 10.0;
        m_scaleFactor = 1.0;
        update();
        return true;
    }
    case Pending::ScaleTarget: {
        const double distance = std::hypot(scenePoint.x() - m_moveBase.x(),
                                           scenePoint.y() - m_moveBase.y());
        m_pending = Pending::None;
        applyScale(distance / m_scaleRadius);
        update();
        return true;
    }
    case Pending::MeasureDistance: {
        m_measurePoints.append(scenePoint);
        if (m_measurePoints.size() < 2) {
            update();
            return true;
        }
        const QString report = measureReport();
        m_pending = Pending::None;
        m_measurePoints.clear();
        Q_EMIT statusMessage(report);
        Q_EMIT measured(report);
        update();
        return true;
    }
    case Pending::CutTarget: {
        Folio *folio = m_document->currentFolio();
        m_pending = Pending::None;
        if (!folio) {
            update();
            return true;
        }
        for (const QString &id : std::as_const(m_selection)) {
            const auto cut = EditTools::cut(*folio, id, scenePoint);
            if (!cut)
                continue;
            const auto *pattern = dynamic_cast<const Wire *>(folio->entity(id));
            if (!pattern)
                continue;
            // Les deux morceaux heritent du fil coupe : couper ne fait pas
            // perdre son repere ni son type au conducteur.
            const Wire model = *pattern;
            m_document->pushMacro(tr("Couper le fil"), [&] {
                m_document->push(std::make_unique<RemoveEntityCommand>(
                        m_document->project(), folio->id(), id, tr("Couper le fil")));
                for (const QVector<QPointF> &piece : { cut->before, cut->after }) {
                    auto part = std::make_unique<Wire>(model);
                    part->setId(newId());
                    part->points = piece;
                    m_document->push(std::make_unique<AddEntityCommand>(
                            m_document->project(), folio->id(), std::move(part),
                            tr("Couper le fil")));
                }
            });
            setSelection({});
            Q_EMIT statusMessage(tr("Fil coupé."));
            update();
            return true;
        }
        Q_EMIT statusMessage(tr("Coupure : ce point n'est pas sur le fil sélectionné."));
        update();
        return true;
    }
    case Pending::StretchBase:
        m_moveBase = scenePoint;
        m_pending = Pending::StretchTarget;
        update();
        return true;
    case Pending::StretchTarget: {
        const QPointF delta = scenePoint - m_moveBase;
        m_pending = Pending::None;
        if (!samePoint(delta, QPointF()))
            applyStretch(delta);
        m_stretchWindow = QRectF();
        update();
        return true;
    }
    }
    return false;
}

void FolioView::applyOffset(const QPointF &sidePoint)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;

    // Seuls les fils se decalent : decaler un symbole n'a pas de sens, c'est
    // une copie deplacee, et l'outil existe deja pour cela.
    std::vector<EntityPtr> copies;
    for (const QString &id : std::as_const(m_selection)) {
        const auto *wire = dynamic_cast<const Wire *>(folio->entity(id));
        if (!wire || wire->points.size() < 2)
            continue;

        // Le cote se decide sur le premier segment : de quel bord de la
        // droite porteuse se trouve le point clique.
        const QPointF a = wire->points.first();
        const QPointF b = wire->points.at(1);
        QPointF direction = b - a;
        const double length = std::hypot(direction.x(), direction.y());
        if (length <= 1e-9)
            continue;
        direction /= length;
        const QPointF normal(-direction.y(), direction.x());
        const QPointF toClick = sidePoint - a;
        const double side = toClick.x() * normal.x() + toClick.y() * normal.y();
        const QPointF shift = normal * (side < 0.0 ? -m_offsetDistance : m_offsetDistance);

        auto copy = std::make_unique<Wire>(*wire);
        copy->setId(newId());
        // Le repere ne se recopie pas : le fil decale est un autre conducteur,
        // et lui laisser le repere de l'original fausserait le reperage.
        copy->number.clear();
        copy->numberLocked = false;
        copy->translate(shift);
        copies.push_back(std::move(copy));
    }

    if (copies.empty()) {
        Q_EMIT statusMessage(tr("Décaler : aucun fil dans la sélection."));
        return;
    }

    const int count = int(copies.size());
    QSet<QString> created;
    // Un decalage multiple doit se defaire d'une seule annulation.
    m_document->pushMacro(tr("Décaler"), [&] {
        for (auto &copy : copies) {
            created.insert(copy->id());
            m_document->push(std::make_unique<AddEntityCommand>(m_document->project(),
                                                                folio->id(), std::move(copy),
                                                                tr("Décaler")));
        }
    });
    // La selection suit la copie : on enchaine souvent plusieurs decalages.
    setSelection(created);
    Q_EMIT statusMessage(tr("%n fil(s) décalé(s) de %1 mm.", "", count)
                                 .arg(m_offsetDistance, 0, 'f', 2));
}

void FolioView::setShapeStroke(Primitive::Stroke stroke)
{
    m_shapeStroke = stroke;
    // Le geste d'AutoCAD : le style choisi vaut pour la suite, et s'applique
    // tout de suite a ce qui est designe. Choisir « pointille » avec un cadre
    // selectionne doit changer CE cadre, sinon il faut le retracer.
    if (!m_selection.isEmpty())
        applyStrokeToSelection(stroke);
    update();
}

int FolioView::applyStrokeToSelection(Primitive::Stroke stroke)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return 0;

    std::vector<std::pair<EntityPtr, EntityPtr>> changes;
    for (const QString &id : std::as_const(m_selection)) {
        const auto *item = dynamic_cast<const GraphicItem *>(folio->entity(id));
        if (!item || item->shape.stroke == stroke)
            continue;
        auto after = std::make_unique<GraphicItem>(*item);
        after->shape.stroke = stroke;
        changes.emplace_back(item->clone(), std::move(after));
    }

    if (changes.empty())
        return 0;

    const int count = int(changes.size());
    m_document->pushMacro(tr("Changer le style de trait"), [&] {
        for (auto &change : changes) {
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), std::move(change.first),
                    std::move(change.second), tr("Changer le style de trait")));
        }
    });
    Q_EMIT statusMessage(tr("%n forme(s) passée(s) au nouveau trait.", "", count));
    return count;
}

int FolioView::applyWireTypeToSelection(const QString &wireTypeId)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return 0;

    std::vector<std::pair<EntityPtr, EntityPtr>> changes;
    for (const QString &id : std::as_const(m_selection)) {
        const auto *wire = dynamic_cast<const Wire *>(folio->entity(id));
        if (!wire || wire->wireType == wireTypeId)
            continue;
        auto after = std::make_unique<Wire>(*wire);
        after->wireType = wireTypeId;
        changes.emplace_back(wire->clone(), std::move(after));
    }

    if (changes.empty()) {
        Q_EMIT statusMessage(tr("Type de fil : aucun fil à changer dans la sélection."));
        return 0;
    }

    const int count = int(changes.size());
    m_document->pushMacro(tr("Changer le type de fil"), [&] {
        for (auto &change : changes) {
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), std::move(change.first),
                    std::move(change.second), tr("Changer le type de fil")));
        }
    });
    Q_EMIT statusMessage(tr("%n fil(s) passé(s) au nouveau type.", "", count));
    return count;
}

int FolioView::setSelectionTagsLocked(bool locked)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return 0;

    std::vector<std::pair<EntityPtr, EntityPtr>> changes;
    int skipped = 0;
    for (const QString &id : std::as_const(m_selection)) {
        const Entity *entity = folio->entity(id);
        if (const auto *wire = dynamic_cast<const Wire *>(entity)) {
            if (wire->numberLocked == locked)
                continue;
            // Fixer un repere vide ne fixe rien : la renumerotation ignore un
            // verrou sans repere, et le cadenas afficherait une promesse que
            // personne ne tient. Liberer, en revanche, marche toujours.
            if (locked && wire->number.isEmpty()) {
                ++skipped;
                continue;
            }
            auto after = std::make_unique<Wire>(*wire);
            after->numberLocked = locked;
            changes.emplace_back(wire->clone(), std::move(after));
        } else if (const auto *symbol = dynamic_cast<const SymbolInstance *>(entity)) {
            if (symbol->designationLocked == locked)
                continue;
            if (locked && symbol->designation().isEmpty()) {
                ++skipped;
                continue;
            }
            auto after = std::make_unique<SymbolInstance>(*symbol);
            after->designationLocked = locked;
            changes.emplace_back(symbol->clone(), std::move(after));
        }
    }

    if (changes.empty()) {
        Q_EMIT statusMessage(skipped > 0
                                     ? tr("Rien à fixer : %n élément(s) sans repère.", "",
                                          skipped)
                                     : tr("Repères : rien à changer dans la sélection."));
        return 0;
    }

    const int count = int(changes.size());
    const QString label = locked ? tr("Fixer les repères") : tr("Libérer les repères");
    m_document->pushMacro(label, [&] {
        for (auto &change : changes) {
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), std::move(change.first),
                    std::move(change.second), label));
        }
    });

    QString message = locked ? tr("%n repère(s) fixé(s) — la renumérotation ne les touchera "
                                 "plus.", "", count)
                             : tr("%n repère(s) libéré(s).", "", count);
    if (skipped > 0)
        message += tr(" %n élément(s) sans repère laissé(s) de côté.", "", skipped);
    Q_EMIT statusMessage(message);
    return count;
}

void FolioView::copySelection()
{
    m_clipboard.clear();
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    for (const QString &id : std::as_const(m_selection)) {
        if (const Entity *entity = folio->entity(id))
            m_clipboard.push_back(entity->clone());
    }
    Q_EMIT clipboardChanged();
    Q_EMIT statusMessage(tr("%n élément(s) copié(s)", "", int(m_clipboard.size())));
}

void FolioView::pasteClipboard(bool keepTags)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_clipboard.empty())
        return;

    // Le collage se pose sous le curseur, decale du coin haut-gauche du lot :
    // coller au meme endroit que l'original cache ce qu'on vient de coller.
    QRectF bounds;
    for (const EntityPtr &entity : m_clipboard) {
        const QRectF b = entity->boundingBox();
        bounds = bounds.isNull() ? b : bounds.united(b);
    }
    const QPointF offset = snap(m_cursorMm) - snapToGrid(bounds.topLeft(), m_style.gridStep);

    // Les copies sont construites, deplacees, puis re-reperees AVANT d'entrer
    // dans le document : rien n'y entre en double, meme le temps d'une
    // commande, et le collage reste une seule annulation. Le re-reperage a
    // besoin de la position finale — la reference de ligne en depend.
    std::vector<EntityPtr> copies;
    copies.reserve(m_clipboard.size());
    for (const EntityPtr &source : m_clipboard) {
        EntityPtr copy = source->clone();
        copy->setId(newId());
        copy->translate(offset);
        copies.push_back(std::move(copy));
    }

    CircuitCopyResult retagged;
    if (!keepTags) {
        static const PlcDatabase kNoModules;
        retagged = CircuitCopy::retag(copies, m_document->project(), m_document->profile(),
                                      m_plc ? *m_plc : kNoModules, folio);
    }

    QSet<QString> pasted;
    m_document->pushMacro(tr("Coller"), [&] {
        for (EntityPtr &copy : copies) {
            pasted.insert(copy->id());
            m_document->push(std::make_unique<AddEntityCommand>(m_document->project(),
                                                                folio->id(), std::move(copy),
                                                                tr("Coller")));
        }
    });
    setSelection(pasted);

    // Dire ce qui a change : un collage muet laisse croire que les reperes ont
    // ete conserves, et l'erreur ne se voit qu'a la nomenclature.
    const int count = int(pasted.size());
    if (keepTags) {
        Q_EMIT statusMessage(tr("%n élément(s) collé(s) à l'identique — repères conservés.",
                                "", count));
    } else if (retagged.total() > 0) {
        Q_EMIT statusMessage(tr("%n élément(s) collé(s) — %1.", "", count)
                                     .arg(retagged.summary()));
    } else {
        Q_EMIT statusMessage(tr("%n élément(s) collé(s)", "", count));
    }
}

void FolioView::highlightNetOfSelection()
{
    m_highlight.clear();
    const Folio *folio = m_document->currentFolio();
    if (!folio || m_selection.isEmpty()) {
        update();
        return;
    }

    const Netlist &netlist = m_document->netlist();
    QSet<int> netIds;
    for (const QString &id : std::as_const(m_selection)) {
        if (const Netlist::Net *net = netlist.netOfWire(id))
            netIds.insert(net->id);
        if (const auto *symbol = dynamic_cast<const SymbolInstance *>(folio->entity(id))) {
            const SymbolDefinition *definition =
                    m_document->project().library.definition(symbol->definitionId);
            if (!definition)
                continue;
            for (const Pin &pin : definition->pins) {
                if (const Netlist::Net *net = netlist.netOfPin(symbol->id(), pin.number))
                    netIds.insert(net->id);
            }
        }
    }

    QStringList names;
    for (int id : netIds) {
        const Netlist::Net *net = netlist.net(id);
        if (!net)
            continue;
        for (const Netlist::WireRef &ref : net->wires)
            m_highlight.insert(ref.wireId);
        if (!net->number.isEmpty())
            names.append(net->number);
        else if (!net->name.isEmpty())
            names.append(net->name);
    }

    Q_EMIT statusMessage(names.isEmpty()
                                 ? tr("Aucun potentiel sous la sélection")
                                 : tr("Potentiel : %1").arg(names.join(QStringLiteral(", "))));
    update();
}

void FolioView::clearHighlight()
{
    m_highlight.clear();
    update();
}

// --------------------------------------------------------------------------
// Creation d'entites

void FolioView::beginWireAt(const QPointF &point)
{
    m_wirePoints.clear();
    m_wirePoints.append(point);
}

void FolioView::commitWire()
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_wirePoints.size() < 2) {
        m_wirePoints.clear();
        update();
        return;
    }

    // Les traces : un seul, ou les N conducteurs paralleles du bus. Le reste
    // du chemin est identique — le bus n'est pas un autre outil, c'est le
    // meme geste repete.
    QVector<QVector<QPointF>> paths;
    if (m_bus)
        paths = WireTools::busPaths(m_wirePoints, *m_bus);
    if (paths.isEmpty())
        paths.append(m_wirePoints);

    std::vector<Wire> snapshots;
    std::vector<EntityPtr> wires;
    snapshots.reserve(paths.size());
    for (int k = 0; k < paths.size(); ++k) {
        auto wire = std::make_unique<Wire>();
        wire->points = paths.at(k);
        wire->wireType = m_currentWireType;
        // Le nom du conducteur est ce qui fait qu'un L1 se raccorde a un L1
        // et jamais a un L2 : la netlist apparie par nom des qu'il y en a un.
        if (m_bus) {
            const QString name = m_bus->conductorAt(k);
            if (!name.isEmpty())
                wire->conductors = QStringList{ name };
        }
        snapshots.push_back(*wire);
        wires.push_back(std::move(wire));
    }

    const int count = int(wires.size());
    // Un bus doit se defaire d'une seule annulation : trois conducteurs poses
    // et deux annulations pour les retirer serait un piege.
    m_document->pushMacro(count > 1 ? tr("Tracer un fil multiple") : tr("Tracer un fil"), [&] {
        for (auto &wire : wires) {
            m_document->push(std::make_unique<AddEntityCommand>(
                    m_document->project(), folio->id(), std::move(wire), tr("Tracer un fil")));
        }
        for (const Wire &snapshot : snapshots)
            addImplicitJunctions(snapshot);
    });

    if (count > 1)
        Q_EMIT statusMessage(tr("%n conducteur(s) tracé(s).", "", count));

    m_wirePoints.clear();
    m_snapEngine.clearTracked();
    m_trackHit.reset();
    update();
}

void FolioView::addImplicitJunctions(const Wire &wire)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || wire.points.size() < 2)
        return;

    const QPointF ends[2] = { wire.points.first(), wire.points.last() };
    for (const QPointF &end : ends) {
        bool alreadyMarked = false;
        for (const Junction *junction : folio->entitiesOfType<Junction>()) {
            if (samePoint(junction->point, end)) {
                alreadyMarked = true;
                break;
            }
        }
        if (alreadyMarked)
            continue;

        // Une extremite posee au milieu d'un autre fil est deja connectee au
        // sens electrique ; le point de jonction ne fait que la rendre visible.
        bool onOtherSegment = false;
        for (const Wire *other : folio->entitiesOfType<Wire>()) {
            if (other->id() == wire.id())
                continue;
            bool onVertex = false;
            for (const QPointF &vertex : other->points) {
                if (samePoint(vertex, end)) {
                    onVertex = true;
                    break;
                }
            }
            if (onVertex)
                continue;
            for (int i = 1; i < other->points.size(); ++i) {
                if (pointOnSegment(end, other->points.at(i - 1), other->points.at(i))) {
                    onOtherSegment = true;
                    break;
                }
            }
            if (onOtherSegment)
                break;
        }
        if (!onOtherSegment)
            continue;

        auto junction = std::make_unique<Junction>();
        junction->point = end;
        m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                            std::move(junction),
                                                            tr("Poser une jonction")));
    }
}

void FolioView::placeSymbolAt(const QPointF &point)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_pendingSymbol.isEmpty())
        return;

    m_lastSymbol = m_pendingSymbol;
    m_lastOrientation = m_pendingPlacement.orientation;

    auto instance = std::make_unique<SymbolInstance>();
    instance->definitionId = m_pendingSymbol;
    instance->placement = m_pendingPlacement;
    instance->placement.position = point;
    if (const SymbolDefinition *definition =
                m_document->project().library.definition(m_pendingSymbol)) {
        instance->setLocalBounds(definition->bounds());
        instance->fields = definition->defaultFields;
    }
    // Le prototype passe apres les champs par defaut : il les complete plutot
    // que de les remplacer, et c'est lui qui gagne quand les deux parlent du
    // meme champ.
    if (m_pendingPrototype) {
        for (auto it = m_pendingPrototype->fields.cbegin();
             it != m_pendingPrototype->fields.cend(); ++it) {
            instance->fields.insert(it.key(), it.value());
        }
        instance->designationLocked = m_pendingPrototype->designationLocked;
        if (!m_pendingPrototype->deviceGroup.isEmpty())
            instance->deviceGroup = m_pendingPrototype->deviceGroup;
    }
    const QString id = instance->id();

    // Poser un appareil de passage sur un fil doit le brancher, pas se poser
    // par-dessus : le fil est coupe et rebranche sur les broches, comme le
    // fait AutoCAD Electrical. La coupure se calcule sur l'appareil deja pose,
    // donc apres l'ajout — et le tout tient dans une seule annulation.
    const SymbolInstance model = *instance;
    m_document->pushMacro(tr("Poser un symbole"), [&] {
        m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                            std::move(instance),
                                                            tr("Poser un symbole")));
        const auto split = ComponentTools::splitForInsertion(*folio,
                                                             m_document->project().library,
                                                             model);
        if (!split)
            return;
        const auto *wire = dynamic_cast<const Wire *>(folio->entity(split->wireId));
        if (!wire)
            return;

        // Les morceaux heritent du fil d'origine : repere, conducteurs, type.
        // Brancher un appareil ne doit pas faire perdre son identite au fil.
        const Wire pattern = *wire;
        m_document->push(std::make_unique<RemoveEntityCommand>(m_document->project(),
                                                               folio->id(), split->wireId,
                                                               tr("Brancher sur le fil")));
        for (const QVector<QPointF> &piece : { split->before, split->after }) {
            if (piece.size() < 2)
                continue;
            auto part = std::make_unique<Wire>(pattern);
            part->setId(newId());
            part->points = piece;
            m_document->push(std::make_unique<AddEntityCommand>(m_document->project(),
                                                                folio->id(), std::move(part),
                                                                tr("Brancher sur le fil")));
        }
        Q_EMIT statusMessage(tr("Appareil branché : le fil a été coupé sur ses bornes."));
    });

    // Un symbole ordinaire reste arme : on en pose souvent plusieurs a la
    // suite. Un symbole prototype, non — il a ete regle pour cette pose-ci,
    // et poser deux fois la meme carte d'automate donnerait deux modules a la
    // meme adresse.
    if (m_pendingPrototype) {
        m_pendingSymbol.clear();
        m_pendingPrototype.reset();
    }

    setSelection({ id });
    Q_EMIT componentPlaced(id);
}

void FolioView::placeJunctionAt(const QPointF &point)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    for (const Junction *junction : folio->entitiesOfType<Junction>()) {
        if (samePoint(junction->point, point))
            return; // ne pas empiler deux jonctions au meme endroit
    }
    auto junction = std::make_unique<Junction>();
    junction->point = point;
    m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                        std::move(junction),
                                                        tr("Poser une jonction")));
}

void FolioView::setLabelRole(Label::Role role)
{
    m_labelRole = role;
    // Une fleche de signal renvoie a une autre page : lui laisser une portee
    // locale la rendrait muette.
    if (role != Label::Role::Plain)
        m_labelScope = Label::Scope::Project;
}

void FolioView::placeLabelAt(const QPointF &point)
{
    if (!m_document->currentFolio())
        return;
    beginTextEntry(point, true);
}

void FolioView::placeTextAt(const QPointF &point)
{
    if (!m_document->currentFolio())
        return;
    beginTextEntry(point, false);
}

// --------------------------------------------------------------------------
// LA SAISIE DE TEXTE SUR PLACE
//
// On tape ou l'on a clique, a la taille reelle, sur le dessin. Voir
// folioview.h pour le pourquoi : la boite modale coupait le dessin en deux a
// chaque annotation et cachait l'endroit meme ou le texte allait se poser.

void FolioView::beginTextEntry(const QPointF &point, bool label)
{
    m_textEntry = true;
    m_textIsLabel = label;
    m_textPoint = point;
    m_textTyped.clear();
    setFocus();
    update();
}

void FolioView::cancelTextEntry()
{
    if (!m_textEntry)
        return;
    m_textEntry = false;
    m_textTyped.clear();
    update();
}

void FolioView::commitTextEntry()
{
    if (!m_textEntry)
        return;
    const QString contenu = m_textTyped.trimmed();
    const QPointF point = m_textPoint;
    const bool label = m_textIsLabel;
    m_textEntry = false;
    m_textTyped.clear();

    Folio *folio = m_document->currentFolio();
    if (!folio || contenu.isEmpty()) {
        update();
        return;
    }

    if (label) {
        auto etiquette = std::make_unique<Label>();
        etiquette->point = point;
        etiquette->name = contenu;
        etiquette->role = m_labelRole;
        etiquette->scope =
                m_labelRole == Label::Role::Plain ? m_labelScope : Label::Scope::Project;
        // Une destination pointe vers le dessin, une source s'en eloigne : le
        // sens de lecture doit correspondre au sens du signal.
        etiquette->direction =
                m_labelRole == Label::Role::Destination ? Direction::Left : Direction::Right;
        m_document->push(std::make_unique<AddEntityCommand>(
                m_document->project(), folio->id(), std::move(etiquette),
                m_labelRole == Label::Role::Plain ? tr("Poser une étiquette")
                                                  : tr("Poser une flèche de signal")));
    } else {
        auto texte = std::make_unique<TextItem>();
        texte->placement.position = point;
        texte->text = contenu;
        // La hauteur retenue s'applique : on ecrit rarement une seule ligne
        // dans une taille donnee, et la reprendre a chaque fois par la fiche
        // de proprietes coutait deux gestes par texte.
        texte->height = m_textHeight;
        m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                            std::move(texte),
                                                            tr("Ajouter un texte")));
    }
    update();
}

bool FolioView::handleTextKey(QKeyEvent *event)
{
    if (!m_textEntry)
        return false;
    switch (event->key()) {
    case Qt::Key_Escape:
        cancelTextEntry();
        Q_EMIT statusMessage(tr("Texte abandonné."));
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        commitTextEntry();
        return true;
    case Qt::Key_Backspace:
        m_textTyped.chop(1);
        update();
        return true;
    default:
        break;
    }
    // Tout caractere imprimable entre dans le texte, y compris les chiffres :
    // pendant une saisie de texte, « 8 » est un huit, pas un raccourci.
    if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
        m_textTyped += event->text();
        update();
        return true;
    }
    return false;
}

void FolioView::setTextHeight(double heightMm)
{
    if (heightMm <= 0.0)
        return;
    m_textHeight = heightMm;
    if (!m_selection.isEmpty())
        applyTextHeightToSelection(heightMm);
    update();
}

int FolioView::applyTextHeightToSelection(double heightMm)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || heightMm <= 0.0)
        return 0;

    std::vector<std::pair<EntityPtr, EntityPtr>> changes;
    for (const QString &id : std::as_const(m_selection)) {
        const auto *texte = dynamic_cast<const TextItem *>(folio->entity(id));
        if (!texte || fuzzyEqual(texte->height, heightMm))
            continue;
        auto after = std::make_unique<TextItem>(*texte);
        after->height = heightMm;
        changes.emplace_back(texte->clone(), std::move(after));
    }
    if (changes.empty())
        return 0;

    const int count = int(changes.size());
    m_document->pushMacro(tr("Changer la hauteur du texte"), [&] {
        for (auto &change : changes) {
            m_document->push(std::make_unique<ModifyEntityCommand>(
                    m_document->project(), folio->id(), std::move(change.first),
                    std::move(change.second), tr("Changer la hauteur du texte")));
        }
    });
    Q_EMIT statusMessage(tr("%n texte(s) à la nouvelle hauteur.", "", count));
    return count;
}

void FolioView::trimAt(const QPointF &point)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    const auto *wire = dynamic_cast<const Wire *>(entityAt(point));
    if (!wire) {
        Q_EMIT statusMessage(tr("Ajuster : visez le fil à couper."));
        return;
    }

    const auto result = WireTools::trim(*folio, m_document->project().library, wire->id(), point);
    if (!result)
        return;

    // Les morceaux heritent des caracteristiques du fil d'origine : repere,
    // conducteurs, verrouillage. Ajuster un fil ne doit pas lui faire perdre
    // son identite electrique.
    const Wire model = *wire;
    const QString removedId = wire->id();
    const QVector<QVector<QPointF>> pieces = result->pieces;

    m_document->pushMacro(tr("Ajuster un fil"), [&] {
        m_document->push(std::make_unique<RemoveEntityCommand>(m_document->project(),
                                                               folio->id(), removedId,
                                                               tr("Ajuster un fil")));
        for (const QVector<QPointF> &piece : pieces) {
            auto replacement = std::make_unique<Wire>(model);
            replacement->setId(newId());
            replacement->points = piece;
            m_document->push(std::make_unique<AddEntityCommand>(m_document->project(),
                                                                folio->id(),
                                                                std::move(replacement),
                                                                tr("Ajuster un fil")));
        }
    });

    clearSelection();
    Q_EMIT statusMessage(pieces.isEmpty()
                                 ? tr("Fil supprimé : aucun croisement pour le couper.")
                                 : tr("Fil ajusté en %n morceau(x).", "", int(pieces.size())));
}

void FolioView::extendAt(const QPointF &point)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    auto *wire = dynamic_cast<Wire *>(entityAt(point));
    if (!wire || wire->points.size() < 2) {
        Q_EMIT statusMessage(tr("Prolonger : visez le fil, près de l'extrémité à allonger."));
        return;
    }

    // L'extremite visee est la plus proche du clic : on prolonge le bout
    // qu'on montre, pas l'autre.
    const double toFirst = std::hypot(point.x() - wire->points.first().x(),
                                      point.y() - wire->points.first().y());
    const double toLast = std::hypot(point.x() - wire->points.last().x(),
                                     point.y() - wire->points.last().y());
    const bool lastEnd = toLast <= toFirst;

    const auto target = WireTools::extend(*folio, m_document->project().library, wire->id(),
                                          lastEnd);
    if (!target) {
        Q_EMIT statusMessage(tr("Rien à atteindre dans l'axe de ce fil."));
        return;
    }

    auto before = wire->clone();
    auto after = wire->clone();
    auto *edited = static_cast<Wire *>(after.get());
    if (lastEnd)
        edited->points.last() = *target;
    else
        edited->points.first() = *target;

    m_document->push(std::make_unique<ModifyEntityCommand>(m_document->project(), folio->id(),
                                                           std::move(before), std::move(after),
                                                           tr("Prolonger un fil")));
    Q_EMIT statusMessage(tr("Fil prolongé."));
}

// --------------------------------------------------------------------------
// Evenements souris

void FolioView::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    const QPointF widgetPoint = event->position();
    const QPointF scenePoint = toScene(widgetPoint);
    const QPointF snapped = snap(scenePoint);

    if (event->button() == Qt::MiddleButton
        || ((m_spaceHeld || m_panArmed) && event->button() == Qt::LeftButton)) {
        m_drag = Drag::Pan;
        m_dragStartWidget = widgetPoint;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::RightButton) {
        // Le clic droit termine le geste en cours plutot que d'ouvrir un menu
        // au milieu d'un trace.
        if (!m_wirePoints.isEmpty()) {
            commitWire();
            return;
        }
        if (abandonGesture(false))
            return;
        // Rien en cours : c'est le menu contextuel. Comme dans AutoCAD, un
        // clic droit sur une entite non selectionnee la designe d'abord —
        // sinon la moitie du menu s'appliquerait a une autre selection.
        if (Entity *entity = entityAt(scenePoint)) {
            if (!m_selection.contains(entity->id()))
                setSelection({ entity->id() });
        }
        Q_EMIT contextMenuRequested(event->globalPosition().toPoint());
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    if (applyPointAt(snapped))
        return;

    if (m_zoomWindowArmed) {
        m_drag = Drag::ZoomWindow;
        m_dragStartScene = scenePoint;
        m_rubber = QRectF(scenePoint, scenePoint);
        return;
    }

    if (m_stretchArmed) {
        m_drag = Drag::StretchWindow;
        m_dragStartScene = scenePoint;
        m_rubber = QRectF(scenePoint, scenePoint);
        return;
    }

    switch (m_tool) {
    // Le texte se pose a l'oeil : la resolution ne s'applique pas a lui.
    case Tool::Text:
        placeAt(snapAnnotation(scenePoint));
        return;

    case Tool::Wire:
    case Tool::Symbol:
    case Tool::Junction:
    case Tool::Label:
    case Tool::Line:
    case Tool::Rectangle:
    case Tool::Circle:
    case Tool::Arc:
    case Tool::Polyline:
        placeAt(snapped);
        return;

    case Tool::Trim:
        // L'ajustement vise le trait, pas un point accroche : c'est la
        // position brute qui designe le fil a couper.
        trimAt(scenePoint);
        return;

    case Tool::Extend:
        extendAt(scenePoint);
        return;

    case Tool::Select:
        break;
    }

    // Une poignee sous le curseur prend la main sur tout le reste : c'est le
    // geste le plus precis, il ne doit pas etre vole par la selection.
    const int grip = gripAt(scenePoint);
    if (grip >= 0) {
        m_draggedGrip = grip;
        m_drag = Drag::GripEdit;
        m_movedSinceCommit = false;
        update();
        return;
    }

    Entity *hit = entityAt(scenePoint);
    const bool additive = event->modifiers() & Qt::ShiftModifier;
    // Ctrl designe un membre seul a l'interieur d'un groupe, comme chez
    // AutoCAD : sans cette porte de sortie, un element groupe par erreur
    // devient impossible a rattraper autrement qu'en degroupant tout.
    const bool intoGroup = event->modifiers() & Qt::ControlModifier;

    if (hit) {
        const QSet<QString> touched = intoGroup ? QSet<QString>{ hit->id() }
                                                : QSet<QString>{ hit->id() };
        if (additive) {
            QSet<QString> updated = m_selection;
            if (updated.contains(hit->id()))
                updated.subtract(touched);
            else
                updated.unite(touched);
            setSelection(updated);
        } else if (!m_selection.contains(hit->id())) {
            setSelection(touched);
        }
        m_drag = Drag::Move;
        m_dragStartScene = snapped;
        m_dragLastScene = snapped;
        m_movedSinceCommit = false;
    } else {
        if (!additive)
            clearSelection();
        m_drag = Drag::Rubber;
        m_dragStartScene = scenePoint;
        m_rubber = QRectF(scenePoint, scenePoint);
        m_rubberCrossing = false;
    }
    update();
}

void FolioView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF widgetPoint = event->position();
    const QPointF scenePoint = toScene(widgetPoint);
    m_cursorMm = scenePoint;
    m_cursorInside = true;
    m_snapHit = resolveSnap(scenePoint);
    // Le repere d'alignement ne se cherche que faute d'accrochage : un point
    // du dessin vaut toujours mieux qu'un point calcule.
    m_trackHit = m_snapHit ? std::nullopt : resolveTrack(scenePoint);
    updateAcquisition(scenePoint);
    emitCursor();

    switch (m_drag) {
    case Drag::GripEdit:
        dragGripTo(snap(scenePoint));
        m_movedSinceCommit = true;
        return;

    case Drag::Pan:
        m_pan += widgetPoint - m_dragStartWidget;
        m_dragStartWidget = widgetPoint;
        update();
        return;

    case Drag::Move: {
        const QPointF snapped = snap(scenePoint);
        const QPointF delta = snapped - m_dragLastScene;
        if (samePoint(delta, QPointF(0, 0), 1e-9))
            return;
        Folio *folio = m_document->currentFolio();
        if (!folio)
            return;
        const QStringList ids(m_selection.cbegin(), m_selection.cend());
        // Les deplacements successifs fusionnent : un glisser ne laisse qu'une
        // seule entree dans l'historique.
        m_document->push(std::make_unique<MoveEntitiesCommand>(m_document->project(),
                                                               folio->id(), ids, delta));
        m_dragLastScene = snapped;
        m_movedSinceCommit = true;
        return;
    }

    case Drag::ZoomWindow:
    case Drag::StretchWindow:
        m_rubber = normalized(m_dragStartScene, scenePoint);
        update();
        return;

    case Drag::Rubber:
        m_rubber = normalized(m_dragStartScene, scenePoint);
        // Le sens du geste decide du mode : vers la gauche, c'est une capture.
        m_rubberCrossing = scenePoint.x() < m_dragStartScene.x();
        update();
        return;

    case Drag::None:
        break;
    }

    const int hovered = m_tool == Tool::Select ? gripAt(scenePoint) : -1;
    if (hovered != m_hotGrip) {
        m_hotGrip = hovered;
        setCursor(hovered >= 0 ? Qt::SizeAllCursor : Qt::BlankCursor);
    }

    // Le retour d'accrochage change a chaque deplacement : la vue doit se
    // repeindre meme au repos, sinon le marqueur reste colle a sa position
    // precedente.
    update();
}

void FolioView::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    switch (m_drag) {
    case Drag::Pan:
        setCursor(Qt::BlankCursor);
        break;
    case Drag::Move:
        if (m_movedSinceCommit)
            m_document->commands().breakMergeChain();
        break;
    case Drag::GripEdit:
        if (m_movedSinceCommit)
            m_document->commands().breakMergeChain();
        m_draggedGrip = -1;
        rebuildGrips();
        break;
    case Drag::ZoomWindow: {
        const QRectF target = m_rubber;
        m_rubber = QRectF();
        m_zoomWindowArmed = false;
        setCursor(Qt::BlankCursor);
        // Un simple clic sans glisser n'est pas une fenetre : on annule
        // plutot que de zoomer sur un point et de perdre l'utilisateur.
        if (target.width() > 1e-3 && target.height() > 1e-3)
            zoomToRect(target);
        break;
    }

    case Drag::StretchWindow: {
        const QRectF window = m_rubber;
        m_rubber = QRectF();
        m_stretchArmed = false;
        setCursor(Qt::BlankCursor);
        if (window.width() <= 1e-3 || window.height() <= 1e-3) {
            Q_EMIT statusMessage(tr("Étirer annulé : il faut encadrer une zone."));
            break;
        }
        m_stretchWindow = window;
        // La selection suit la fenetre : l'utilisateur voit ce qu'il vient de
        // prendre avant de designer les deux points.
        setSelection(entitiesIn(window, true));
        m_pending = Pending::StretchBase;
        break;
    }

    case Drag::Rubber: {
        QSet<QString> found = entitiesIn(m_rubber, m_rubberCrossing);
        if (m_pending == Pending::PickObjects) {
            // Pendant une designation, la fenetre AJOUTE et ne retient que ce
            // que la commande sait traiter : encadrer un depart entier pour
            // couper un fil ne doit pas embarquer les appareils.
            const Folio *folio = m_document->currentFolio();
            QSet<QString> kept = m_selection;
            for (const QString &id : std::as_const(found)) {
                if (folio && matchesFilter(folio->entity(id), m_pickFilter))
                    kept.insert(id);
            }
            setSelection(kept);
            m_rubber = QRectF();
            m_drag = Drag::None;
            update();
            return;
        }
        if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
            found.unite(m_selection);
        setSelection(found);
        m_rubber = QRectF();
        break;
    }
    case Drag::None:
        break;
    }
    m_drag = Drag::None;
    update();
}

void FolioView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_tool == Tool::Wire && !m_wirePoints.isEmpty()) {
        commitWire();
        return;
    }
    if (Entity *hit = entityAt(toScene(event->position())))
        Q_EMIT entityActivated(hit->id());
    else
        Q_EMIT folioActivated();
}

void FolioView::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (fuzzyZero(steps))
        return;
    setZoom(m_scale * std::pow(1.2, steps), event->position());
    event->accept();
}

bool FolioView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && (m_textEntry || m_typing)) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool commande = key->modifiers() & (Qt::ControlModifier | Qt::AltModifier
                                                  | Qt::MetaModifier);
        if (!commande) {
            // La touche nous revient comme une frappe ordinaire.
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void FolioView::keyPressEvent(QKeyEvent *event)
{
    // La saisie de texte passe avant TOUT : pendant qu'on ecrit, « 8 » est un
    // huit, « E » une lettre, et Echap n'abandonne que le texte.
    if (handleTextKey(event))
        return;

    // Puis la cote tapee, pour la meme raison.
    if (handleTypedKey(event))
        return;

    const double step = event->modifiers() & Qt::ShiftModifier ? m_style.gridStep * 4.0
                                                               : m_style.gridStep;
    switch (event->key()) {
    case Qt::Key_Escape:
        abandonGesture();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Entree valide la designation et rend la main a la commande, comme
        // chez AutoCAD.
        if (m_pending == Pending::PickObjects) {
            finishPick();
            return;
        }
        if (m_tool == Tool::Polyline && m_shapePoints.size() >= 2) {
            commitShape();
            return;
        }
        if (!m_wirePoints.isEmpty())
            commitWire();
        return;
    case Qt::Key_Backspace:
        // Retire le dernier coude plutot que d'annuler tout le trace.
        if (m_shapePoints.size() > 1) {
            m_shapePoints.removeLast();
            update();
            return;
        }
        if (m_wirePoints.size() > 1) {
            m_wirePoints.removeLast();
            update();
            return;
        }
        break;
    case Qt::Key_Delete:
        deleteSelection();
        return;
    case Qt::Key_Space:
        m_spaceHeld = true;
        setCursor(Qt::OpenHandCursor);
        return;
    case Qt::Key_R:
        rotateSelection(!(event->modifiers() & Qt::ShiftModifier));
        return;
    case Qt::Key_M:
        mirrorSelection();
        return;
    case Qt::Key_Left:
        nudgeSelection(QPointF(-step, 0));
        return;
    case Qt::Key_Right:
        nudgeSelection(QPointF(step, 0));
        return;
    case Qt::Key_Up:
        nudgeSelection(QPointF(0, -step));
        return;
    case Qt::Key_Down:
        nudgeSelection(QPointF(0, step));
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void FolioView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        m_spaceHeld = false;
        setCursor(Qt::BlankCursor);
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void FolioView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_fitPending)
        zoomToFit();
    update();
}

void FolioView::leaveEvent(QEvent *event)
{
    if (m_acquireTimer)
        m_acquireTimer->stop();
    m_hoverCandidate.reset();
    m_trackHit.reset();
    m_cursorInside = false;
    m_snapHit.reset();
    update();
    QWidget::leaveEvent(event);
}

void FolioView::emitCursor()
{
    const Folio *folio = m_document->currentFolio();
    Q_EMIT cursorMoved(m_cursorMm, folio ? folio->zoneAt(m_cursorMm) : QString());
}

void FolioView::updateUnconnectedPins()
{
    m_unconnectedPins.clear();
    const Folio *folio = m_document->currentFolio();
    if (!folio || !m_style.showUnconnectedPins)
        return;
    for (const Netlist::PinRef &pin : m_document->netlist().unconnectedPins()) {
        if (pin.folioId == folio->id())
            m_unconnectedPins.append(pin.position);
    }
}

void FolioView::updateCrossReferences()
{
    // Le renvoi se deduit du dessin entier : deplacer une fleche sur un folio
    // change ce qu'affiche celle de l'autre bout.
    m_crossRefs = CrossReference::resolve(m_document->project(), m_document->netlist());
}

// --------------------------------------------------------------------------
// Trace

void FolioView::paintPendingWire(QPainter &painter) const
{
    if (m_wirePoints.isEmpty())
        return;

    QVector<QPointF> preview = m_wirePoints;
    // Le fantome suit la cote tapee des qu'il y en a une : sans cela on tape
    // une longueur sans voir ou elle mene.
    preview.append(committedPoint());

    QPen pen(m_style.wire);
    pen.setWidthF(m_style.wireWidth);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Le fantome montre TOUS les conducteurs du bus : voir un seul trait puis
    // en obtenir trois, c'est decouvrir le resultat par l'annulation.
    QVector<QVector<QPointF>> paths;
    if (m_bus)
        paths = WireTools::busPaths(preview, *m_bus);
    if (paths.isEmpty())
        paths.append(preview);
    for (const QVector<QPointF> &path : paths)
        painter.drawPolyline(path.constData(), int(path.size()));
}

void FolioView::paintTextEntry(QPainter &painter) const
{
    if (!m_textEntry)
        return;
    // Compose en UNITES DE DESSIN et a la hauteur reelle : c'est tout
    // l'interet de taper sur place plutot que dans une boite. On voit si le
    // texte tient dans la place AVANT de le valider.
    painter.save();
    const double hauteur = m_textIsLabel ? 2.5 : m_textHeight;
    painter.setPen(m_style.selection);
    const QString affiche = m_textTyped + QStringLiteral("▏");
    FolioPainter::drawTextMm(painter, m_textPoint, affiche, hauteur);
    painter.restore();
}

void FolioView::paintPendingGesture(QPainter &painter) const
{
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return;

    if (m_pending == Pending::MeasureDistance) {
        paintMeasure(painter);
        return;
    }

    if (m_pending == Pending::PickObjects) {
        // L'invite est ecrite sous le curseur, pas seulement dans la barre
        // d'etat : c'est la qu'on regarde pendant qu'on designe, et c'est tout
        // le defaut qu'on corrige. Elle rappelle aussi les deux touches qui
        // terminent — sans quoi on ne sait pas comment sortir.
        painter.save();
        // ATTENTION : paintPendingGesture peint dans le repere du DESSIN
        // (millimetres). L'invite, elle, se pose en pixels sous le curseur —
        // sa taille ne doit pas dependre du zoom. D'ou la remise a plat de la
        // transformation, comme le fait la saisie dynamique.
        painter.resetTransform();
        painter.setFont(Theme::uiFont(9));
        const QFontMetricsF metrics(painter.font());
        // Tant que rien n'est designe, l'invite dit quoi faire. Des qu'on a
        // designe, elle dit ou l'on en est et comment finir : repeter l'invite
        // ferait une phrase trop longue pour se lire du coin de l'oeil.
        const QString line = m_selection.isEmpty()
                ? m_pickPrompt
                : tr("%n désigné(s) · Entrée valide", "", int(m_selection.size()));
        const QSizeF size(metrics.horizontalAdvance(line) + 16, metrics.height() + 10);
        QRectF box(toWidget(m_cursorMm) + QPointF(18, -26), size);
        // Rabattue dans la vue : pres d'un bord, l'invite sortait du widget et
        // se coupait — c'est justement au bord qu'on designe le dernier fil.
        const QRectF inside = QRectF(rect()).adjusted(4, 4, -4, -4);
        box.moveLeft(std::clamp(box.left(), inside.left(), inside.right() - box.width()));
        box.moveTop(std::clamp(box.top(), inside.top(), inside.bottom() - box.height()));
        // Le fond est celui de la FEUILLE, pas du pourtour : c'est au-dessus
        // du dessin que l'invite se pose, et la couleur du texte est celle qui
        // s'y lit deja — les deux contrastent par construction.
        QColor fill = m_style.sheet;
        fill.setAlpha(235);
        // Le filet est celui de la selection, c'est-a-dire l'accent : l'invite
        // sous le curseur et celle de la ligne de commande disent la meme
        // chose et se reconnaissent a la meme couleur. Le vert du reperage
        // d'accrochage voulait dire autre chose.
        painter.setPen(QPen(m_style.selection, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(box, 4.0, 4.0);
        painter.setPen(m_style.text);
        painter.drawText(box, Qt::AlignCenter, line);
        painter.restore();
    }

    if (m_pending == Pending::MoveTarget) {
        // Fantome de la selection sous le curseur : sans lui, on choisit le
        // point d'arrivee a l'aveugle.
        const QPointF delta = snap(m_cursorMm) - m_moveBase;
        painter.save();
        QPen pen(m_style.selection);
        pen.setWidthF(m_style.wireWidth);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const QString &id : m_selection) {
            const Entity *entity = folio->entity(id);
            if (!entity)
                continue;
            if (const auto *wire = dynamic_cast<const Wire *>(entity)) {
                QVector<QPointF> ghost = wire->points;
                for (QPointF &p : ghost)
                    p += delta;
                painter.drawPolyline(ghost.constData(), int(ghost.size()));
            } else {
                painter.drawRect(entity->boundingBox().translated(delta));
            }
        }
        painter.drawLine(m_moveBase, m_moveBase + delta);
        painter.restore();
        return;
    }

    if (m_pending == Pending::ScaleTarget) {
        // Fantome homothetique de la selection, plus le facteur en clair.
        // Grossir a l'aveugle et regarder ensuite oblige a annuler une fois
        // sur deux.
        const QPointF cursor = snap(m_cursorMm);
        const double distance = std::hypot(cursor.x() - m_moveBase.x(),
                                           cursor.y() - m_moveBase.y());
        const double factor = m_scaleRadius > kConnectTolerance ? distance / m_scaleRadius : 1.0;

        painter.save();
        QPen pen(m_style.selection);
        pen.setWidthF(m_style.wireWidth);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const QString &id : m_selection) {
            const Entity *entity = folio->entity(id);
            if (!entity)
                continue;
            if (const auto *wire = dynamic_cast<const Wire *>(entity)) {
                QVector<QPointF> ghost = wire->points;
                for (QPointF &p : ghost)
                    p = scaledAbout(p, m_moveBase, factor);
                painter.drawPolyline(ghost.constData(), int(ghost.size()));
            } else {
                const QRectF box = entity->boundingBox();
                painter.drawRect(QRectF(scaledAbout(box.topLeft(), m_moveBase, factor),
                                        scaledAbout(box.bottomRight(), m_moveBase, factor)));
            }
        }

        // Le point fixe, marque comme tel : c'est lui qu'on choisit, et il
        // n'a aucune raison d'etre au centre de la selection.
        QPen anchor(m_style.snapMarker);
        anchor.setWidthF(m_style.wireWidth * 1.2);
        painter.setPen(anchor);
        const double r = 1.6;
        painter.drawLine(m_moveBase + QPointF(-r, 0), m_moveBase + QPointF(r, 0));
        painter.drawLine(m_moveBase + QPointF(0, -r), m_moveBase + QPointF(0, r));
        painter.drawLine(m_moveBase, cursor);
        painter.restore();
        return;
    }

    if (m_pending == Pending::ScootTarget || m_pending == Pending::ComponentTarget) {
        const auto *symbol = dynamic_cast<const SymbolInstance *>(folio->entity(m_componentId));
        if (!symbol)
            return;
        QPointF delta = snap(m_cursorMm) - m_componentStart;
        if (m_pending == Pending::ScootTarget && m_scootAxis)
            delta = ComponentTools::constrainToAxis(delta, *m_scootAxis);

        painter.save();
        QPen pen(m_style.selection);
        pen.setWidthF(m_style.wireWidth);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(symbol->boundingBox().translated(delta));

        // Les fils raccordes suivent : l'apercu doit le montrer, sinon on
        // croit deplacer l'appareil seul.
        for (const ComponentTools::WireEnd &end :
             ComponentTools::attachedWireEnds(*folio, m_document->project().library, *symbol)) {
            const auto *wire = dynamic_cast<const Wire *>(folio->entity(end.wireId));
            if (!wire)
                continue;
            QVector<QPointF> ghost = wire->points;
            if (end.vertex >= 0 && end.vertex < ghost.size())
                ghost[end.vertex] += delta;
            painter.drawPolyline(ghost.constData(), int(ghost.size()));
        }

        // L'axe de glissement se montre : c'est lui qui explique pourquoi
        // l'appareil refuse de quitter sa ligne.
        if (m_pending == Pending::ScootTarget && m_scootAxis) {
            QPen guide(m_style.snapGuide);
            guide.setWidthF(std::max(0.12, 1.0 / m_scale));
            guide.setStyle(Qt::DotLine);
            painter.setPen(guide);
            const QPointF reach = *m_scootAxis * (400.0 / m_scale + 60.0);
            painter.drawLine(m_componentStart - reach, m_componentStart + reach);
        }
        painter.restore();
        return;
    }

    if (m_pending == Pending::StretchTarget) {
        // Apercu du resultat : seuls les sommets pris dans la fenetre bougent.
        // Sans lui, on ne voit pas ce que la fenetre a reellement saisi.
        const QPointF delta = snap(m_cursorMm) - m_moveBase;
        painter.save();
        QPen pen(m_style.selection);
        pen.setWidthF(m_style.wireWidth);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const EntityPtr &entity : folio->entities()) {
            if (const auto *wire = dynamic_cast<const Wire *>(entity.get())) {
                QVector<QPointF> ghost = wire->points;
                bool touched = false;
                for (QPointF &p : ghost) {
                    if (m_stretchWindow.contains(p)) {
                        p += delta;
                        touched = true;
                    }
                }
                if (touched)
                    painter.drawPolyline(ghost.constData(), int(ghost.size()));
            } else if (m_stretchWindow.contains(entity->boundingBox().center())) {
                painter.drawRect(entity->boundingBox().translated(delta));
            }
        }
        // La fenetre reste affichee pendant la designation des deux points.
        QPen frame(QColor(0x5C, 0xB8, 0x5C));
        frame.setWidthF(0.2);
        frame.setStyle(Qt::DotLine);
        painter.setPen(frame);
        painter.drawRect(m_stretchWindow);
        painter.drawLine(m_moveBase, m_moveBase + delta);
        painter.restore();
        return;
    }

    if (m_pending == Pending::StretchBase && !m_stretchWindow.isNull()) {
        painter.save();
        QPen frame(QColor(0x5C, 0xB8, 0x5C));
        frame.setWidthF(0.2);
        frame.setStyle(Qt::DotLine);
        painter.setPen(frame);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_stretchWindow);
        painter.restore();
        return;
    }

    if (m_pending == Pending::OffsetSide) {
        // Apercu du fil decale du cote ou pointe le curseur.
        painter.save();
        QPen pen(m_style.selection);
        pen.setWidthF(m_style.wireWidth);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QPointF side = snap(m_cursorMm);
        for (const QString &id : m_selection) {
            const auto *wire = dynamic_cast<const Wire *>(folio->entity(id));
            if (!wire || wire->points.size() < 2)
                continue;
            const QPointF a = wire->points.first();
            QPointF direction = wire->points.at(1) - a;
            const double length = std::hypot(direction.x(), direction.y());
            if (length <= 1e-9)
                continue;
            direction /= length;
            const QPointF normal(-direction.y(), direction.x());
            const QPointF toCursor = side - a;
            const double dot = toCursor.x() * normal.x() + toCursor.y() * normal.y();
            const QPointF shift = normal * (dot < 0.0 ? -m_offsetDistance : m_offsetDistance);
            QVector<QPointF> ghost = wire->points;
            for (QPointF &p : ghost)
                p += shift;
            painter.drawPolyline(ghost.constData(), int(ghost.size()));
        }
        painter.restore();
    }
}

void FolioView::paintPendingSymbol(QPainter &painter) const
{
    if (m_pendingSymbol.isEmpty())
        return;
    const SymbolDefinition *definition =
            m_document->project().library.definition(m_pendingSymbol);
    if (!definition)
        return;

    Placement placement = m_pendingPlacement;
    placement.position = snap(m_cursorMm);
    const Transform2D t = placement.transform();

    painter.save();
    painter.setOpacity(0.55);
    painter.setWorldTransform(QTransform(t.m11, t.m12, t.m21, t.m22, t.dx, t.dy), true);
    RenderStyle ghost = m_style;
    ghost.symbol = m_style.selection;
    FolioPainter::paintDefinition(painter, *definition, ghost);
    painter.restore();
}

void FolioView::paintPolarGuide(QPainter &painter) const
{
    const QPointF *from = gestureOrigin();
    if (!from)
        return;
    const auto angle = m_snapEngine.constrainedAngle(*from, m_cursorMm);
    if (!angle)
        return;

    // Le rayon d'alignement d'AutoCAD : un trait fin qui traverse la feuille
    // et montre le cap suivi. Sans lui, la contrainte agit sans s'expliquer.
    const double radians = *angle * std::numbers::pi / 180.0;
    const QPointF direction(std::cos(radians), std::sin(radians));
    const QRectF view = visibleSceneRect();
    const double reach = std::hypot(view.width(), view.height());

    painter.save();
    QPen pen(m_style.snapGuide);
    pen.setWidthF(0.18);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.drawLine(*from - direction * reach, *from + direction * reach);
    painter.restore();
}

void FolioView::paintSnapFeedback(QPainter &painter) const
{
    if (!m_snapHit)
        return;
    // Une poignee sous le curseur prend la main sur le marqueur d'accrochage :
    // les deux se superposeraient au meme point, et c'est la poignee qui dit
    // ce que le clic va faire.
    if (m_hotGrip >= 0 || m_draggedGrip >= 0)
        return;

    // Le prolongement s'explique par un trait pointille jusqu'a l'extremite
    // dont il part : sans lui, le marqueur flotte sans raison apparente.
    if (m_snapHit->hasOrigin) {
        painter.save();
        QPen pen(m_style.snapGuide);
        pen.setWidthF(0.18);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(m_snapHit->origin, m_snapHit->point);
        painter.restore();
    }

    // La taille du marqueur est fixee a l'ecran, pas dans le dessin : il doit
    // garder le meme encombrement quel que soit le zoom.
    const double size = std::max(m_style.snapMarkerSize, 13.0 / m_scale);
    FolioPainter::paintSnapMarker(painter, m_snapHit->mode, m_snapHit->point, size,
                                  m_style.snapMarker);

    // L'etiquette nomme le mode, comme l'info-bulle AutoSnap : c'est elle qui
    // apprend les modes a qui ne les connait pas encore.
    painter.save();
    painter.setPen(m_style.snapMarker);
    FolioPainter::drawTextMm(painter, m_snapHit->point + QPointF(size * 0.85, size * 1.5),
                             m_snapHit->label(), std::max(2.0, 11.0 / m_scale));
    painter.restore();
}

void FolioView::paintTracking(QPainter &painter) const
{
    const auto &tracked = m_snapEngine.trackedPoints();
    if (tracked.isEmpty())
        return;

    // Taille fixee a l'ecran, comme les marqueurs d'accrochage : un repere
    // doit garder le meme encombrement a tous les zooms.
    const double size = std::max(m_style.snapMarkerSize * 0.8, 10.0 / m_scale);

    painter.save();
    // Le marqueur du repere acquis : la petite croix d'AutoCAD. Elle dit
    // « ce point est retenu », rien de plus, et doit rester discrete.
    QPen mark(m_style.snapMarker);
    mark.setWidthF(std::max(0.22, 2.0 / m_scale));
    painter.setPen(mark);
    painter.setBrush(Qt::NoBrush);
    for (const TrackedPoint &point : tracked) {
        painter.drawLine(point.point + QPointF(-size, 0.0), point.point + QPointF(size, 0.0));
        painter.drawLine(point.point + QPointF(0.0, -size), point.point + QPointF(0.0, size));
    }

    // Les traits d'alignement ne se tracent que lorsqu'on est dessus : les
    // afficher en permanence couvrirait le folio de pointilles.
    if (m_trackHit) {
        QPen guide(m_style.snapGuide);
        guide.setWidthF(std::max(0.12, 1.0 / m_scale));
        guide.setStyle(Qt::DotLine);
        painter.setPen(guide);

        auto ray = [&](const QPointF &origin) {
            // Le trait depasse le point vise : c'est ce prolongement qui
            // montre qu'il s'agit d'un alignement et non d'un segment.
            const QPointF delta = m_trackHit->point - origin;
            const double length = std::hypot(delta.x(), delta.y());
            if (length < 1e-6)
                return;
            const QPointF direction = delta / length;
            painter.drawLine(origin, m_trackHit->point + direction * (12.0 / m_scale));
        };
        ray(m_trackHit->origin);
        if (m_trackHit->hasSecond)
            ray(m_trackHit->secondOrigin);
        if (m_trackHit->crossesConstraint)
            ray(m_trackHit->constraintOrigin);

        // Marqueur et etiquette au point retenu, comme pour un accrochage :
        // c'est la meme promesse, « le clic tombera ici ».
        const double markerSize = std::max(m_style.snapMarkerSize, 13.0 / m_scale);
        FolioPainter::paintSnapMarker(painter, m_trackHit->originMode, m_trackHit->point,
                                      markerSize, m_style.snapGuide);
        painter.setPen(m_style.snapGuide);
        const QString label = m_trackHit->isCrossing()
                ? tr("%1 : croisement").arg(snapModeName(m_trackHit->originMode))
                : tr("%1 : alignement").arg(snapModeName(m_trackHit->originMode));
        FolioPainter::drawTextMm(painter,
                                 m_trackHit->point + QPointF(markerSize * 0.85,
                                                             markerSize * 1.5),
                                 label, std::max(2.0, 11.0 / m_scale));
    }
    painter.restore();
}

void FolioView::paintRubberBand(QPainter &painter) const
{
    if (m_drag == Drag::StretchWindow && !m_rubber.isNull()) {
        // ETIRER designe toujours par capture : le cadre reprend donc le vert
        // pointille de la capture, pour dire que ce qu'il effleure sera pris.
        const QColor color(0x5C, 0xB8, 0x5C);
        QPen pen(color);
        pen.setWidthF(0.25);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 38));
        painter.drawRect(m_rubber);
        return;
    }
    if (m_drag == Drag::ZoomWindow && !m_rubber.isNull()) {
        // Le cadre de zoom ne selectionne rien : il se distingue donc des
        // cadres de selection, bleu et vert, par sa couleur d'accent.
        QPen pen(m_style.selection);
        pen.setWidthF(0.25);
        pen.setStyle(Qt::DashDotLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_rubber);
        return;
    }
    if (m_drag != Drag::Rubber || m_rubber.isNull())
        return;

    // Les codes d'AutoCAD, appris par des millions de dessinateurs : bleu et
    // trait plein pour la fenetre, vert et pointille pour la capture. La
    // couleur annonce le mode avant que le clic ne soit relache.
    const QColor color = m_rubberCrossing ? QColor(0x5C, 0xB8, 0x5C) : QColor(0x4C, 0x8F, 0xD4);
    QPen pen(color);
    pen.setWidthF(0.25);
    pen.setStyle(m_rubberCrossing ? Qt::DashLine : Qt::SolidLine);
    painter.setPen(pen);
    painter.setBrush(QColor(color.red(), color.green(), color.blue(), 38));
    painter.drawRect(m_rubber);
}

void FolioView::paintCrosshair(QPainter &painter) const
{
    if (!m_style.showCrosshair || !m_cursorInside)
        return;

    const QPointF centre = toWidget(m_cursorMm);
    const double reach = std::max(width(), height())
            * std::clamp(m_style.crosshairPercent, 5.0, 100.0) / 100.0;

    painter.save();
    QPen pen(m_style.crosshair);
    pen.setWidthF(0.8);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(centre.x() - reach, centre.y()),
                     QPointF(centre.x() + reach, centre.y()));
    painter.drawLine(QPointF(centre.x(), centre.y() - reach),
                     QPointF(centre.x(), centre.y() + reach));

    // Le carre de selection au centre : il montre la zone effectivement
    // testee au clic, ce qui explique pourquoi on attrape ou non un objet.
    if (m_tool == Tool::Select) {
        const double h = m_style.pickBoxPixels / 2.0;
        painter.drawRect(QRectF(centre.x() - h, centre.y() - h, m_style.pickBoxPixels,
                                m_style.pickBoxPixels));
    }
    painter.restore();
}

void FolioView::paintDynamicInput(QPainter &painter) const
{
    if (!m_style.showDynamicInput || (!m_cursorInside && !m_typing))
        return;
    const QPointF *from = gestureOrigin();
    if (!from && !m_typing)
        return;

    const QPointF target = committedPoint();
    QString text;
    bool editing = false;

    if (m_typing) {
        // Pendant la frappe, le champ montre ce qui est tape, pas ce que la
        // souris raconte : c'est le clavier qui commande.
        const bool understood = CoordinateEntry::resolve(m_typed, from, snap(m_cursorMm))
                                        .has_value();
        text = m_typed + QStringLiteral("▏");
        editing = true;
        if (!understood)
            text += QStringLiteral("  ?");
    } else {
        const QPointF delta = target - *from;
        const double length = std::hypot(delta.x(), delta.y());
        if (length < 1e-6)
            return;
        text = QStringLiteral("%1 mm   ∠ %2°")
                       .arg(length, 0, 'f', 1)
                       .arg(CoordinateEntry::screenAngle(delta), 0, 'f', 1);
    }

    const QPointF anchor = toWidget(m_typing ? m_cursorMm : target) + QPointF(16, 20);
    QFont font = painter.font();
    font.setPointSizeF(editing ? 10.5 : 9.0);
    if (editing)
        font.setBold(true);
    const QFontMetricsF metrics(font);
    const QRectF box(anchor, QSizeF(metrics.horizontalAdvance(text) + 18, metrics.height() + 10));

    painter.save();
    painter.setFont(font);
    painter.setPen(Qt::NoPen);
    // Le champ en cours de frappe s'annonce : il a la main sur le geste, il
    // doit se distinguer de l'affichage passif de la cote.
    painter.setBrush(editing ? QColor(0x1F, 0x33, 0x28, 235) : QColor(0, 0, 0, 165));
    painter.drawRoundedRect(box, 5, 5);
    if (editing) {
        QPen border(m_style.snapMarker);
        border.setWidthF(1.4);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, 5, 5);
    }
    painter.setPen(editing ? m_style.snapMarker : QColor(0xF0, 0xF4, 0xF2));
    painter.drawText(box, Qt::AlignCenter, text);
    painter.restore();
}

void FolioView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // Le rendu est le seul point par lequel TOUT changement d'etat passe :
    // armer un outil, poser un point, abandonner un geste appellent tous
    // update(). C'est donc ici qu'on recalcule l'invite, plutot que dans les
    // soixante-quinze endroits qui changent l'etat. La garde de refreshPrompt
    // empeche la boucle : une invite inchangee ne signale rien, et un signal
    // qui redimensionne la ligne de commande ne provoque au pire qu'un second
    // rendu, muet celui-la.
    refreshPrompt();
    QPainter painter(this);
    painter.fillRect(rect(), m_style.pageBackground);

    const Folio *folio = m_document->currentFolio();
    if (!folio) {
        painter.setPen(m_style.text);
        painter.drawText(rect(), Qt::AlignCenter, tr("Aucun folio ouvert"));
        return;
    }

    painter.translate(m_pan);
    painter.scale(m_scale, m_scale);

    // Le peintre a besoin de l'echelle pour espacer la grille : sans elle, une
    // grille trop serree se perd en voile gris, ou disparait tout a fait.
    RenderStyle styleEcran = m_style;
    styleEcran.pixelsPerMm = m_scale;
    FolioPainter folioPainter(m_document->project(), styleEcran);
    folioPainter.setSelection(m_selection);
    folioPainter.setHighlightedEntities(m_highlight);
    folioPainter.setUnconnectedPins(m_unconnectedPins);
    folioPainter.setCrossReferences(m_crossRefs);
    folioPainter.paint(painter, *folio, visibleSceneRect());

    painter.setRenderHint(QPainter::Antialiasing, true);
    paintGrips(painter);
    paintPolarGuide(painter);
    paintPendingWire(painter);
    paintPendingSymbol(painter);
    paintShapePreview(painter);
    paintPendingGesture(painter);
    paintTextEntry(painter);
    paintTracking(painter);
    paintSnapFeedback(painter);
    paintRubberBand(painter);
    painter.resetTransform();
    paintCrosshair(painter);
    paintDynamicInput(painter);
    paintEmptyHint(painter, *folio);
}

// Les etapes montrees sur un folio vide. Elles vivent ici, en un seul
// endroit, parce que la mise en page ET le trace en ont besoin : deux listes
// finiraient par ne plus decrire le meme bloc.
QVector<FolioView::HintStep> FolioView::emptyHintSteps()
{
    // Seulement de vraies touches : une case dessinee comme une touche doit
    // se taper. Le geste a la souris est dit en toutes lettres au-dessus.
    return {
        { tr("W"), tr("tracer un fil — puis tapez la cote : 50, @10,5") },
        { tr("F11"), tr("retenir un point et suivre son alignement") },
        { tr("Ctrl + Maj + P"), tr("chercher n'importe quelle commande par son nom") },
        { tr("F9"), tr("numéroter tous les fils du dossier d'un coup") },
    };
}

FolioView::EmptyHintLayout FolioView::layoutEmptyHint(const Folio &folio) const
{
    EmptyHintLayout layout;
    const QVector<HintStep> steps = emptyHintSteps();
    const QFont keys = Theme::monoFont(font().pointSizeF() * 0.9);
    const QFontMetricsF keyMetrics(keys);
    const QFontMetricsF textMetrics(font());

    // Tout ce qui suit est mesure en UNITES DE DESSIN, jamais en pixels : le
    // bloc est compose une fois pour toutes a cette taille, puis mis a
    // l'echelle de la feuille. Composer directement en pixels liait sa taille
    // a la fenetre, et le conseil changeait de proportion a chaque zoom.
    for (const HintStep &step : steps) {
        layout.keyWidth = std::max(layout.keyWidth,
                                   keyMetrics.horizontalAdvance(step.key) + 2 * kHintCapPadding);
        layout.textWidth = std::max(layout.textWidth,
                                    textMetrics.horizontalAdvance(step.what));
    }
    layout.lineHeight = textMetrics.height() + 9.0;

    const double designWidth = layout.keyWidth + kHintColumnGap + layout.textWidth;
    const double designHeight = kHintTitleHeight + kHintLeadHeight + kHintLeadGap
            + steps.size() * layout.lineHeight;
    layout.design = QRectF(0.0, 0.0, designWidth, designHeight);

    const QRectF sheet = QRectF(toWidget(folio.sheetRect().topLeft()),
                                toWidget(folio.sheetRect().bottomRight()))
                                 .normalized();

    // Le conseil appartient a la FEUILLE : il en occupe une fraction fixe et
    // se centre dessus. Compose a taille de pixel constante, il paraissait
    // enorme sur une feuille dezoomee et minuscule sur une feuille zoomee —
    // c'est ce que l'utilisateur a releve. Lie a la feuille, il garde la meme
    // proportion a tous les zooms.
    layout.scale = 1.0;
    if (sheet.height() > 1.0 && designHeight > 0.0)
        layout.scale = sheet.height() * kHintSheetFraction / designHeight;

    // Un garde-fou, et un seul : zoome tres pres, un dixieme de la feuille
    // deborde largement la fenetre, et un conseil plus grand que la vue
    // n'apprend rien. Il ne joue jamais aux zooms de travail.
    if (designWidth > 0.0 && designHeight > 0.0) {
        layout.scale = std::min(layout.scale,
                                std::min(width() * 0.9 / designWidth,
                                         height() * 0.9 / designHeight));
    }
    layout.scale = std::max(layout.scale, 0.01);

    const QSizeF painted(designWidth * layout.scale, designHeight * layout.scale);
    const QPointF centre = sheet.isEmpty() ? QRectF(rect()).center() : sheet.center();
    layout.block = QRectF(centre.x() - painted.width() / 2.0,
                          centre.y() - painted.height() / 2.0, painted.width(),
                          painted.height());
    return layout;
}

bool FolioView::showsEmptyHint() const
{
    const Folio *folio = m_document->currentFolio();
    return folio && folio->entityCount() == 0 && m_pendingSymbol.isEmpty();
}

QRectF FolioView::emptyHintRect() const
{
    if (!showsEmptyHint())
        return QRectF();
    return layoutEmptyHint(*m_document->currentFolio()).block;
}

void FolioView::paintEmptyHint(QPainter &painter, const Folio &folio) const
{
    // Un folio vide n'apprend rien a qui ouvre le logiciel pour la premiere
    // fois. Quelques lignes suffisent a indiquer par ou commencer, et elles
    // disparaissent des le premier element pose.
    if (folio.entityCount() > 0 || !m_pendingSymbol.isEmpty())
        return;

    QFont title = font();
    title.setPointSizeF(font().pointSizeF() * 1.45);
    title.setWeight(QFont::DemiBold);
    // Les touches sont dessinees comme des touches — encadrees, a chasse
    // fixe, comme dans la palette de commandes. Une meme chose se montre
    // partout de la meme facon, sinon il faut l'apprendre deux fois.
    const QFont keys = Theme::monoFont(font().pointSizeF() * 0.9);
    const QFontMetricsF keyMetrics(keys);

    QColor strong = m_style.text;
    strong.setAlpha(200);
    QColor faint = m_style.text;
    faint.setAlpha(135);
    QColor keyColor = m_style.text;
    keyColor.setAlpha(190);
    QColor capBorder = m_style.text;
    capBorder.setAlpha(60);
    QColor capFill = m_style.pageBackground;
    capFill.setAlpha(150);

    const QVector<HintStep> steps = emptyHintSteps();
    const EmptyHintLayout layout = layoutEmptyHint(folio);

    // Le bloc se dessine dans ses unites de composition, et c'est le peintre
    // qui le met a l'echelle de la feuille. Recalculer chaque coordonnee a la
    // main donnerait le meme resultat au prix d'un facteur repete vingt fois,
    // dont un oublie tot ou tard.
    painter.save();
    painter.translate(layout.block.topLeft());
    painter.scale(layout.scale, layout.scale);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double width = layout.design.width();

    painter.setFont(title);
    painter.setPen(strong);
    painter.drawText(QRectF(0.0, 0.0, width, kHintTitleHeight),
                     Qt::AlignHCenter | Qt::AlignTop,
                     tr("Ce folio est vide — par où commencer"));
    painter.setFont(font());
    painter.setPen(faint);
    painter.drawText(QRectF(0.0, kHintTitleHeight + 2.0, width, kHintLeadHeight),
                     Qt::AlignHCenter | Qt::AlignTop,
                     tr("Posez un appareil en cliquant dans la palette, à gauche."));

    double y = kHintTitleHeight + kHintLeadHeight + kHintLeadGap;
    for (const HintStep &step : steps) {
        painter.setFont(keys);
        const double capWidth = keyMetrics.horizontalAdvance(step.key) + 2 * kHintCapPadding;
        const QRectF cap(layout.keyWidth - capWidth, y + (layout.lineHeight - 21.0) / 2.0,
                         capWidth, 21.0);
        painter.setPen(QPen(capBorder, 1.0));
        painter.setBrush(capFill);
        painter.drawRoundedRect(cap, 4.0, 4.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(keyColor);
        painter.drawText(cap, Qt::AlignCenter, step.key);
        painter.setFont(font());
        painter.setPen(faint);
        painter.drawText(QRectF(layout.keyWidth + kHintColumnGap, y, layout.textWidth,
                                layout.lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, step.what);
        y += layout.lineHeight;
    }
    painter.restore();
}

} // namespace dsn
