#include "folioview.h"

#include "core/documentcommands.h"
#include "render/foliopainter.h"

#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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

QPointF FolioView::snap(const QPointF &scenePoint) const
{
    if (const auto connection = snapToConnection(scenePoint))
        return *connection;
    if (!m_snap)
        return scenePoint;
    return snapToGrid(scenePoint, m_style.gridStep);
}

std::optional<QPointF> FolioView::snapToConnection(const QPointF &scenePoint) const
{
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return std::nullopt;

    // Le rayon d'accrochage est defini a l'ecran, pas dans le dessin : il doit
    // rester confortable a tous les niveaux de zoom.
    const double radius = std::max(m_style.gridStep * 0.45, 8.0 / m_scale);
    double best = radius;
    std::optional<QPointF> found;

    auto consider = [&](const QPointF &candidate) {
        const double distance = std::hypot(candidate.x() - scenePoint.x(),
                                           candidate.y() - scenePoint.y());
        if (distance < best) {
            best = distance;
            found = candidate;
        }
    };

    for (const EntityPtr &entity : folio->entities()) {
        if (const auto *symbol = dynamic_cast<const SymbolInstance *>(entity.get())) {
            const SymbolDefinition *definition =
                    m_document->project().library.definition(symbol->definitionId);
            if (!definition)
                continue;
            for (const Pin &pin : definition->pins) {
                if (pin.type != PinType::NotConnected)
                    consider(symbol->placement.map(pin.position));
            }
        } else if (const auto *wire = dynamic_cast<const Wire *>(entity.get())) {
            for (const QPointF &p : wire->points)
                consider(p);
        } else if (const auto *junction = dynamic_cast<const Junction *>(entity.get())) {
            consider(junction->point);
        } else if (const auto *label = dynamic_cast<const Label *>(entity.get())) {
            consider(label->point);
        }
    }
    return found;
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

QSet<QString> FolioView::entitiesIn(const QRectF &sceneRect) const
{
    QSet<QString> found;
    const Folio *folio = m_document->currentFolio();
    if (!folio)
        return found;
    for (const EntityPtr &entity : folio->entities()) {
        const QRectF bounds = entity->boundingBox();
        // Selection par englobement : le rectangle doit contenir l'entite
        // entiere, sans quoi un balayage rapide emporte la moitie du folio.
        if (!bounds.isNull() && sceneRect.contains(bounds))
            found.insert(entity->id());
    }
    return found;
}

void FolioView::setSelection(const QSet<QString> &ids)
{
    if (m_selection == ids)
        return;
    m_selection = ids;
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
    update();
}

void FolioView::setSnapEnabled(bool enabled)
{
    m_snap = enabled;
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
            const QPointF next = m_snapPoint ? *m_snapPoint : orthogonalize(previous, snapped);
            if (!samePoint(next, previous))
                m_wirePoints.append(next);
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
    }
    update();
}

void FolioView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF widgetPoint = event->position();
    const QPointF scenePoint = toScene(widgetPoint);
    m_cursorMm = scenePoint;
    m_snapPoint = snapToConnection(scenePoint);
    emitCursor();

    switch (m_drag) {
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
        update();
        return;

    case Drag::None:
        break;
    }

    if (m_tool != Tool::Select || !m_wirePoints.isEmpty() || !m_pendingSymbol.isEmpty())
        update();
    else if (m_snapPoint)
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
    case Drag::Rubber: {
        QSet<QString> found = entitiesIn(m_rubber);
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
    m_snapPoint.reset();
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
    const QPointF last = preview.last();
    const QPointF target = m_snapPoint ? *m_snapPoint : orthogonalize(last, snap(m_cursorMm));
    preview.append(target);

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

void FolioView::paintSnapMarker(QPainter &painter) const
{
    if (!m_snapPoint)
        return;
    // Marque d'accrochage : sans elle, on ne sait pas si le point suivant
    // tombera sur la broche ou a cote.
    const double r = 5.0 / m_scale;
    QPen pen(m_style.pinMarker);
    pen.setWidthF(0.3);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(m_snapPoint->x() - r, m_snapPoint->y() - r, r * 2, r * 2));
}

void FolioView::paintRubberBand(QPainter &painter) const
{
    if (m_drag != Drag::Rubber || m_rubber.isNull())
        return;
    QPen pen(m_style.selection);
    pen.setWidthF(0.25);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(QColor(m_style.selection.red(), m_style.selection.green(),
                            m_style.selection.blue(), 40));
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
    paintPendingWire(painter);
    paintPendingSymbol(painter);
    paintSnapMarker(painter);
    paintRubberBand(painter);
}

} // namespace dsn
