#include "folioview.h"

#include "core/componenttools.h"
#include "core/coordinateentry.h"
#include "core/documentcommands.h"
#include "core/wiretools.h"
#include "render/foliopainter.h"
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
    // Un deplacement ou un etirement en a une aussi, des que le point de base
    // est pose : leur second point merite la meme contrainte et la meme cote.
    if (m_pending == Pending::MoveTarget || m_pending == Pending::StretchTarget)
        return &m_moveBase;
    return nullptr;
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
        const QPointF constrained = m_snapEngine.constrain(*from, scenePoint);
        if (m_snapEngine.gridSnapEnabled())
            return m_snapEngine.snapToGridPoint(constrained);
        return constrained;
    }
    if (m_snapEngine.gridSnapEnabled())
        return m_snapEngine.snapToGridPoint(scenePoint);
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
    if (tool != Tool::Symbol)
        m_pendingSymbol.clear();
    setCursor(Qt::BlankCursor);
    Q_EMIT toolChanged(tool);
    update();
}

void FolioView::setPendingSymbol(const QString &definitionId)
{
    m_pendingSymbol = definitionId;
    m_pendingPlacement = Placement();
    if (!definitionId.isEmpty())
        setTool(Tool::Symbol);
    update();
}

void FolioView::cancelPending()
{
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
    Q_EMIT statusMessage(tr("Zoom fenêtre : encadrez la zone à agrandir, Échap pour annuler."));
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

void FolioView::zoomActual() { setZoom(physicalDpiX() / kMmPerInch); }

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
    m_document->pushMacro(tr("Supprimer %n élément(s)", "", int(ids.size())), [&] {
        for (const QString &id : ids)
            m_document->push(std::make_unique<RemoveEntityCommand>(m_document->project(),
                                                                   folio->id(), id));
    });
    clearSelection();
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

void FolioView::beginMoveSelection()
{
    if (m_selection.isEmpty()) {
        Q_EMIT statusMessage(tr("Déplacer : sélectionner d'abord ce qu'il faut déplacer."));
        return;
    }
    setTool(Tool::Select);
    m_pending = Pending::MoveBase;
    Q_EMIT statusMessage(tr("Déplacer : cliquer le point de base."));
    update();
}

void FolioView::beginOffset(double distanceMm)
{
    if (m_selection.isEmpty()) {
        Q_EMIT statusMessage(tr("Décaler : sélectionner d'abord le fil à décaler."));
        return;
    }
    if (distanceMm <= 0.0)
        return;
    setTool(Tool::Select);
    m_offsetDistance = distanceMm;
    m_pending = Pending::OffsetSide;
    Q_EMIT statusMessage(tr("Décaler de %1 mm : cliquer du côté voulu.")
                                 .arg(distanceMm, 0, 'f', 2));
    update();
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
    SymbolInstance *symbol = selectedComponent();
    if (!folio || !symbol) {
        Q_EMIT statusMessage(tr("Glisser : sélectionner d'abord l'appareil à déplacer."));
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
    Q_EMIT statusMessage(tr("Glisser le long du fil : cliquer la nouvelle position."));
    update();
}

void FolioView::beginMoveComponent()
{
    SymbolInstance *symbol = selectedComponent();
    if (!symbol) {
        Q_EMIT statusMessage(tr("Déplacer l'appareil : sélectionner d'abord un appareil."));
        return;
    }
    setTool(Tool::Select);
    m_scootAxis.reset();
    m_componentId = symbol->id();
    m_componentStart = symbol->placement.position;
    m_pending = Pending::ComponentTarget;
    Q_EMIT statusMessage(tr("Déplacer l'appareil : cliquer la nouvelle position. "
                            "Les fils raccordés suivent."));
    update();
}

void FolioView::beginStretch()
{
    setTool(Tool::Select);
    m_pending = Pending::None;
    m_stretchArmed = true;
    m_stretchWindow = QRectF();
    Q_EMIT statusMessage(tr("Étirer : encadrer les sommets à déplacer (fenêtre de capture)."));
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
    const bool commandRunning = !m_wirePoints.isEmpty() || m_pending != Pending::None
            || m_tool == Tool::Symbol || m_tool == Tool::Junction || m_tool == Tool::Label
            || m_tool == Tool::Text || m_tool == Tool::Wire;

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
    Q_EMIT statusMessage(tr("Cote : Entrée pour valider, Échap pour abandonner. "
                            "50 · 50<45 · @10,5 · #120,80"));
    update();
    return true;
}

bool FolioView::handlePendingClick(const QPointF &scenePoint)
{
    switch (m_pending) {
    case Pending::None:
        return false;
    case Pending::MoveBase:
        m_moveBase = scenePoint;
        m_pending = Pending::MoveTarget;
        Q_EMIT statusMessage(tr("Déplacer : cliquer le point d'arrivée."));
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
    case Pending::StretchBase:
        m_moveBase = scenePoint;
        m_pending = Pending::StretchTarget;
        Q_EMIT statusMessage(tr("Étirer : cliquer le point d'arrivée."));
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
    Q_EMIT statusMessage(tr("%n élément(s) copié(s)", "", int(m_clipboard.size())));
}

void FolioView::pasteClipboard()
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

    QSet<QString> pasted;
    m_document->pushMacro(tr("Coller"), [&] {
        for (const EntityPtr &source : m_clipboard) {
            EntityPtr copy = source->clone();
            copy->setId(newId());
            copy->translate(offset);
            pasted.insert(copy->id());
            m_document->push(std::make_unique<AddEntityCommand>(m_document->project(),
                                                                folio->id(), std::move(copy),
                                                                tr("Coller")));
        }
    });
    setSelection(pasted);
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
    Q_EMIT statusMessage(tr("Tracé de fil : cliquez pour poser un coude, "
                            "double-cliquez ou Entrée pour terminer, Échap pour annuler."));
}

void FolioView::commitWire()
{
    Folio *folio = m_document->currentFolio();
    if (!folio || m_wirePoints.size() < 2) {
        m_wirePoints.clear();
        update();
        return;
    }

    auto wire = std::make_unique<Wire>();
    wire->points = m_wirePoints;
    wire->wireType = m_currentWireType;
    const Wire snapshot = *wire;

    m_document->pushMacro(tr("Tracer un fil"), [&] {
        m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                            std::move(wire), tr("Tracer un fil")));
        addImplicitJunctions(snapshot);
    });

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

    auto instance = std::make_unique<SymbolInstance>();
    instance->definitionId = m_pendingSymbol;
    instance->placement = m_pendingPlacement;
    instance->placement.position = point;
    if (const SymbolDefinition *definition =
                m_document->project().library.definition(m_pendingSymbol)) {
        instance->setLocalBounds(definition->bounds());
        instance->fields = definition->defaultFields;
    }
    const QString id = instance->id();

    m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                        std::move(instance),
                                                        tr("Poser un symbole")));
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
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;

    QString title = tr("Étiquette de potentiel");
    QString prompt = tr("Nom du potentiel :");
    if (m_labelRole == Label::Role::Source) {
        title = tr("Flèche de signal — source");
        prompt = tr("Nom de code du signal :");
    } else if (m_labelRole == Label::Role::Destination) {
        title = tr("Flèche de signal — destination");
        prompt = tr("Nom de code du signal :");
    } else if (m_labelScope == Label::Scope::Project) {
        title = tr("Renvoi de folio");
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, title, prompt, QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    auto label = std::make_unique<Label>();
    label->point = point;
    label->name = name.trimmed();
    label->role = m_labelRole;
    label->scope = m_labelRole == Label::Role::Plain ? m_labelScope : Label::Scope::Project;
    // Une destination pointe vers le dessin, une source s'en eloigne : le
    // sens de lecture doit correspondre au sens du signal.
    label->direction = m_labelRole == Label::Role::Destination ? Direction::Left
                                                               : Direction::Right;
    m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                        std::move(label),
                                                        m_labelRole == Label::Role::Plain
                                                                ? tr("Poser une étiquette")
                                                                : tr("Poser une flèche de signal")));
}

void FolioView::placeTextAt(const QPointF &point)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    bool ok = false;
    const QString content = QInputDialog::getText(this, tr("Texte"), tr("Contenu :"),
                                                  QLineEdit::Normal, QString(), &ok);
    if (!ok || content.isEmpty())
        return;

    auto text = std::make_unique<TextItem>();
    text->placement.position = point;
    text->text = content;
    m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                        std::move(text), tr("Ajouter un texte")));
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

    if (event->button() == Qt::MiddleButton || (m_spaceHeld && event->button() == Qt::LeftButton)) {
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
        if (m_pending != Pending::None || m_stretchArmed) {
            m_pending = Pending::None;
            m_stretchArmed = false;
            m_stretchWindow = QRectF();
            m_rubber = QRectF();
            Q_EMIT statusMessage(tr("Geste annulé."));
            update();
            return;
        }
        if (m_tool != Tool::Select) {
            setTool(Tool::Select);
            return;
        }
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
    case Tool::Wire:
    case Tool::Symbol:
    case Tool::Junction:
    case Tool::Label:
    case Tool::Text:
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
    const bool additive = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);

    if (hit) {
        if (additive) {
            QSet<QString> updated = m_selection;
            if (updated.contains(hit->id()))
                updated.remove(hit->id());
            else
                updated.insert(hit->id());
            setSelection(updated);
        } else if (!m_selection.contains(hit->id())) {
            setSelection({ hit->id() });
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
        Q_EMIT statusMessage(tr("Étirer : cliquer le point de base."));
        break;
    }

    case Drag::Rubber: {
        QSet<QString> found = entitiesIn(m_rubber, m_rubberCrossing);
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
}

void FolioView::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (fuzzyZero(steps))
        return;
    setZoom(m_scale * std::pow(1.2, steps), event->position());
    event->accept();
}

void FolioView::keyPressEvent(QKeyEvent *event)
{
    // La cote tapee passe avant tout le reste : pendant une saisie, « 8 » est
    // un chiffre, pas un raccourci.
    if (handleTypedKey(event))
        return;

    const double step = event->modifiers() & Qt::ShiftModifier ? m_style.gridStep * 4.0
                                                               : m_style.gridStep;
    switch (event->key()) {
    case Qt::Key_Escape:
        if (m_zoomWindowArmed) {
            m_zoomWindowArmed = false;
            m_rubber = QRectF();
            setCursor(Qt::BlankCursor);
            update();
            return;
        }
        if (m_pending != Pending::None || m_stretchArmed) {
            m_pending = Pending::None;
            m_stretchArmed = false;
            m_stretchWindow = QRectF();
            m_rubber = QRectF();
            Q_EMIT statusMessage(tr("Geste annulé."));
            update();
            return;
        }
        if (!m_wirePoints.isEmpty())
            cancelPending();
        else if (m_tool != Tool::Select)
            setTool(Tool::Select);
        else
            clearSelection();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!m_wirePoints.isEmpty())
            commitWire();
        return;
    case Qt::Key_Backspace:
        // Retire le dernier coude plutot que d'annuler tout le trace.
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
    painter.drawPolyline(preview.constData(), int(preview.size()));
}

void FolioView::paintPendingGesture(QPainter &painter) const
{
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return;

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

    FolioPainter folioPainter(m_document->project(), m_style);
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
    paintPendingGesture(painter);
    paintTracking(painter);
    paintSnapFeedback(painter);
    paintRubberBand(painter);
    painter.resetTransform();
    paintCrosshair(painter);
    paintDynamicInput(painter);
    paintEmptyHint(painter, *folio);
}

void FolioView::paintEmptyHint(QPainter &painter, const Folio &folio) const
{
    // Un folio vide n'apprend rien a qui ouvre le logiciel pour la premiere
    // fois. Deux lignes suffisent a indiquer par ou commencer, et elles
    // disparaissent des le premier element pose.
    if (folio.entityCount() > 0 || !m_pendingSymbol.isEmpty())
        return;

    const QRectF area(0, height() * 0.62, width(), height() * 0.3);
    QFont title = font();
    title.setPointSizeF(font().pointSizeF() * 1.5);
    title.setWeight(QFont::DemiBold);

    QColor strong = m_style.text;
    strong.setAlpha(190);
    QColor faint = m_style.text;
    faint.setAlpha(120);

    painter.setFont(title);
    painter.setPen(strong);
    painter.drawText(QRectF(area.left(), area.top(), area.width(), 30),
                     Qt::AlignHCenter | Qt::AlignTop, tr("Ce folio est vide"));

    painter.setFont(font());
    painter.setPen(faint);
    painter.drawText(
            QRectF(area.left(), area.top() + 34, area.width(), 60),
            Qt::AlignHCenter | Qt::AlignTop,
            tr("Choisissez un symbole dans la palette pour le poser,\n"
               "ou appuyez sur W pour tracer un fil."));
}

} // namespace dsn
