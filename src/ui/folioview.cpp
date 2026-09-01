#include "folioview.h"

#include "core/documentcommands.h"
#include "render/foliopainter.h"

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
    setCursor(Qt::ArrowCursor);

    connect(m_document, &Document::changed, this, [this] {
        updateUnconnectedPins();
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
    updateUnconnectedPins();
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
    return m_wirePoints.isEmpty() ? nullptr : &m_wirePoints.last();
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

QPointF FolioView::snap(const QPointF &scenePoint) const
{
    if (const auto hit = resolveSnap(scenePoint))
        return hit->point;

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
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
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
    m_scale = target;
    m_pan = anchor - sceneAnchor * m_scale;
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
    const Wire snapshot = *wire;

    m_document->pushMacro(tr("Tracer un fil"), [&] {
        m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                            std::move(wire), tr("Tracer un fil")));
        addImplicitJunctions(snapshot);
    });

    m_wirePoints.clear();
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

void FolioView::placeLabelAt(const QPointF &point)
{
    Folio *folio = m_document->currentFolio();
    if (!folio)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
            this,
            m_labelScope == Label::Scope::Project ? tr("Renvoi de folio") : tr("Étiquette de potentiel"),
            tr("Nom du potentiel :"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    auto label = std::make_unique<Label>();
    label->point = point;
    label->name = name.trimmed();
    label->scope = m_labelScope;
    m_document->push(std::make_unique<AddEntityCommand>(m_document->project(), folio->id(),
                                                        std::move(label),
                                                        tr("Poser une étiquette")));
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
        if (m_tool != Tool::Select) {
            setTool(Tool::Select);
            return;
        }
    }

    if (event->button() != Qt::LeftButton)
        return;

    switch (m_tool) {
    case Tool::Wire:
        if (m_wirePoints.isEmpty()) {
            beginWireAt(snapped);
        } else {
            const QPointF previous = m_wirePoints.last();
            if (!samePoint(snapped, previous))
                m_wirePoints.append(snapped);
        }
        update();
        return;

    case Tool::Symbol:
        placeSymbolAt(snapped);
        // L'outil reste arme : poser dix bornes ne doit pas demander dix
        // allers-retours vers la palette.
        return;

    case Tool::Junction:
        placeJunctionAt(snapped);
        return;

    case Tool::Label:
        placeLabelAt(snapped);
        return;

    case Tool::Text:
        placeTextAt(snapped);
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
    m_snapHit = resolveSnap(scenePoint);
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
        setCursor(hovered >= 0 ? Qt::SizeAllCursor
                               : (m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor));
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
        setCursor(m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
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
    const double step = event->modifiers() & Qt::ShiftModifier ? m_style.gridStep * 4.0
                                                               : m_style.gridStep;
    switch (event->key()) {
    case Qt::Key_Escape:
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
        setCursor(m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
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

// --------------------------------------------------------------------------
// Trace

void FolioView::paintPendingWire(QPainter &painter) const
{
    if (m_wirePoints.isEmpty())
        return;

    QVector<QPointF> preview = m_wirePoints;
    preview.append(snap(m_cursorMm));

    QPen pen(m_style.wire);
    pen.setWidthF(m_style.wireWidth);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(preview.constData(), int(preview.size()));
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

void FolioView::paintRubberBand(QPainter &painter) const
{
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
    folioPainter.paint(painter, *folio, visibleSceneRect());

    painter.setRenderHint(QPainter::Antialiasing, true);
    paintGrips(painter);
    paintPolarGuide(painter);
    paintPendingWire(painter);
    paintPendingSymbol(painter);
    paintSnapFeedback(painter);
    paintRubberBand(painter);
    painter.resetTransform();
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
