#include "symboleditor.h"

#include <numbers>

#include "render/foliopainter.h"
#include "mainwindow.h"
#include "theme.h"
#include "symbols/librarystore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace dsn {

namespace {

// Instantane complet de la definition. Un symbole tient dans quelques
// kilo-octets : l'instantane coute moins cher a ecrire et a relire qu'une
// commande par type de modification, et ne peut pas se desynchroniser.
class DefinitionSnapshot : public Command
{
public:
    DefinitionSnapshot(SymbolDefinition *target, SymbolDefinition before, SymbolDefinition after,
                       QString text)
        : m_target(target), m_before(std::move(before)), m_after(std::move(after)),
          m_text(std::move(text))
    {
    }

    void redo() override { *m_target = m_after; }
    void undo() override { *m_target = m_before; }
    QString text() const override { return m_text; }

private:
    SymbolDefinition *m_target;
    SymbolDefinition m_before;
    SymbolDefinition m_after;
    QString m_text;
};

// Libelle lisible d'une primitive. kindTag sert au format de fichier, pas a
// l'affichage : montrer « line » a l'utilisateur est un aveu d'inachevement.
QString primitiveLabel(const Primitive &primitive)
{
    switch (primitive.kind) {
    case Primitive::Kind::Line: return QObject::tr("Ligne");
    case Primitive::Kind::Polyline: return QObject::tr("Polyligne");
    case Primitive::Kind::Rect: return QObject::tr("Rectangle");
    case Primitive::Kind::Circle: return QObject::tr("Cercle");
    case Primitive::Kind::Arc: return QObject::tr("Arc");
    case Primitive::Kind::Text: return QObject::tr("Texte « %1 »").arg(primitive.text);
    }
    return QObject::tr("Tracé");
}

int pinIndexOf(int selection) { return -selection - 1; }
int selectionForPin(int index) { return -index - 1; }
bool isPinSelection(int selection)
{
    return selection != SymbolCanvas::kNoSelection && selection < 0;
}

} // namespace

// ==========================================================================
// SymbolCanvas

SymbolCanvas::SymbolCanvas(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMinimumSize(420, 340);
    // Le meme style que le canevas principal : un symbole se dessine sur le
    // meme papier que le schema ou il sera pose.
    m_style = MainWindow::buildRenderStyle();
    m_style.gridStep = m_gridStep;
}

void SymbolCanvas::setDefinition(const SymbolDefinition &definition)
{
    m_definition = definition;
    m_commands.clear();
    m_selection = kNoSelection;
    m_pending.clear();
    m_fitPending = true;
    zoomToFit();
    Q_EMIT definitionChanged();
    Q_EMIT selectionChanged(m_selection);
}

void SymbolCanvas::modify(const QString &text,
                          const std::function<void(SymbolDefinition &)> &mutate)
{
    SymbolDefinition after = m_definition;
    mutate(after);
    m_commands.push(std::make_unique<DefinitionSnapshot>(&m_definition, m_definition,
                                                         std::move(after), text));
    Q_EMIT definitionChanged();
    update();
}

void SymbolCanvas::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    m_pending.clear();
    m_tool = tool;
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    Q_EMIT toolChanged(tool);
    update();
}

void SymbolCanvas::setSelection(int index)
{
    if (m_selection == index)
        return;
    m_selection = index;
    Q_EMIT selectionChanged(index);
    update();
}

void SymbolCanvas::setGridStep(double step)
{
    if (step <= 0.0)
        return;
    m_gridStep = step;
    m_style.gridStep = step;
    update();
}

void SymbolCanvas::deleteSelection()
{
    if (m_selection == kNoSelection)
        return;
    const int selection = m_selection;
    if (isPinSelection(selection)) {
        const int index = pinIndexOf(selection);
        if (index < 0 || index >= m_definition.pins.size())
            return;
        modify(tr("Supprimer une broche"),
               [index](SymbolDefinition &d) { d.pins.remove(index); });
    } else {
        if (selection < 0 || selection >= m_definition.graphics.size())
            return;
        modify(tr("Supprimer un tracé"),
               [selection](SymbolDefinition &d) { d.graphics.remove(selection); });
    }
    setSelection(kNoSelection);
}

QPointF SymbolCanvas::toScene(const QPointF &widgetPoint) const
{
    return (widgetPoint - m_pan) / m_scale;
}

QPointF SymbolCanvas::toWidget(const QPointF &scenePoint) const
{
    return scenePoint * m_scale + m_pan;
}

QPointF SymbolCanvas::snap(const QPointF &scenePoint) const
{
    return snapToGrid(scenePoint, m_gridStep);
}

void SymbolCanvas::zoomToFit()
{
    if (width() < 50 || height() < 50) {
        m_fitPending = true;
        return;
    }
    m_fitPending = false;
    QRectF bounds = m_definition.bounds();
    if (bounds.isNull() || bounds.width() < 1.0 || bounds.height() < 1.0)
        bounds = QRectF(-15, -15, 30, 30);
    bounds = bounds.adjusted(-6, -6, 6, 6);
    m_scale = std::clamp(std::min(width() / bounds.width(), height() / bounds.height()), 1.0,
                         60.0);
    m_pan = QPointF(width() / 2.0, height() / 2.0) - bounds.center() * m_scale;
    update();
}

int SymbolCanvas::hitTest(const QPointF &scenePoint) const
{
    const double tolerance = std::max(0.8, 5.0 / m_scale);

    // Les broches passent avant le graphisme : ce sont elles qu'on ajuste le
    // plus souvent, et elles sont plus petites.
    for (int i = m_definition.pins.size() - 1; i >= 0; --i) {
        const Pin &pin = m_definition.pins.at(i);
        if (pointOnSegment(scenePoint, pin.root(), pin.position, tolerance))
            return selectionForPin(i);
    }
    for (int i = m_definition.graphics.size() - 1; i >= 0; --i) {
        const Primitive &primitive = m_definition.graphics.at(i);
        const QRectF bounds =
                primitive.bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance);
        if (bounds.contains(scenePoint))
            return i;
    }
    return kNoSelection;
}

void SymbolCanvas::commitPending()
{
    if (m_pending.size() < 2) {
        m_pending.clear();
        update();
        return;
    }

    const QVector<QPointF> points = m_pending;
    const Tool tool = m_tool;
    m_pending.clear();

    modify(tr("Ajouter un tracé"), [tool, points](SymbolDefinition &d) {
        switch (tool) {
        case Tool::Line:
            d.graphics.append(Primitive::line(points.first(), points.at(1)));
            break;
        case Tool::Polyline:
            d.graphics.append(Primitive::polyline(points));
            break;
        case Tool::Rect:
            d.graphics.append(Primitive::rect(normalized(points.first(), points.last())));
            break;
        case Tool::Circle: {
            const QPointF centre = points.first();
            const QPointF edge = points.last();
            d.graphics.append(Primitive::circle(
                    centre, std::hypot(edge.x() - centre.x(), edge.y() - centre.y())));
            break;
        }
        case Tool::Arc: {
            const QPointF centre = points.first();
            const QPointF edge = points.last();
            // L'arc se cree sur un quart de tour depuis le point vise ;
            // l'angle exact se regle ensuite dans le panneau.
            const double radius = std::hypot(edge.x() - centre.x(), edge.y() - centre.y());
            const double start = std::atan2(-(edge.y() - centre.y()), edge.x() - centre.x())
                    * 180.0 / std::numbers::pi;
            d.graphics.append(Primitive::arc(centre, radius, start, 90.0));
            break;
        }
        default:
            break;
        }
    });

    setSelection(int(m_definition.graphics.size()) - 1);
    update();
}

void SymbolCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    const QPointF scenePoint = toScene(event->position());
    const QPointF snapped = snap(scenePoint);

    if (event->button() == Qt::MiddleButton) {
        m_dragging = true;
        m_dragLast = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    switch (m_tool) {
    case Tool::Select: {
        const int hit = hitTest(scenePoint);
        setSelection(hit);
        if (hit != kNoSelection) {
            m_dragging = true;
            m_dragLast = snapped;
        }
        return;
    }
    case Tool::Pin: {
        const int number = int(m_definition.pins.size()) + 1;
        modify(tr("Ajouter une broche"), [snapped, number](SymbolDefinition &d) {
            Pin pin;
            pin.number = QString::number(number);
            pin.position = snapped;
            // Le sens par defaut pointe vers l'exterieur du corps : c'est
            // presque toujours ce qu'on veut.
            pin.direction = std::abs(snapped.x()) >= std::abs(snapped.y())
                    ? (snapped.x() >= 0 ? Direction::Right : Direction::Left)
                    : (snapped.y() >= 0 ? Direction::Down : Direction::Up);
            pin.length = 2.5;
            d.pins.append(pin);
        });
        setSelection(selectionForPin(int(m_definition.pins.size()) - 1));
        return;
    }
    case Tool::Text: {
        bool ok = false;
        const QString content = QInputDialog::getText(this, tr("Texte du symbole"),
                                                      tr("Contenu :"), QLineEdit::Normal,
                                                      QString(), &ok);
        if (!ok || content.isEmpty())
            return;
        modify(tr("Ajouter un texte"), [snapped, content](SymbolDefinition &d) {
            d.graphics.append(Primitive::label(snapped, content, 2.5, Primitive::Align::Center));
        });
        setSelection(int(m_definition.graphics.size()) - 1);
        return;
    }
    case Tool::Polyline:
        if (m_pending.isEmpty())
            m_pending.append(snapped);
        else if (!samePoint(m_pending.last(), snapped))
            m_pending.append(snapped);
        update();
        return;
    default:
        // Ligne, rectangle, cercle, arc : deux points suffisent.
        if (m_pending.isEmpty()) {
            m_pending.append(snapped);
        } else {
            m_pending.append(snapped);
            commitPending();
        }
        update();
        return;
    }
}

void SymbolCanvas::mouseMoveEvent(QMouseEvent *event)
{
    m_cursor = toScene(event->position());

    if (m_dragging && m_tool == Tool::Select && m_selection != kNoSelection) {
        const QPointF snapped = snap(m_cursor);
        const QPointF delta = snapped - m_dragLast;
        if (!samePoint(delta, QPointF(0, 0), 1e-9)) {
            const int selection = m_selection;
            modify(tr("Déplacer"), [selection, delta](SymbolDefinition &d) {
                if (isPinSelection(selection)) {
                    const int index = pinIndexOf(selection);
                    if (index >= 0 && index < d.pins.size())
                        d.pins[index].position += delta;
                } else if (selection >= 0 && selection < d.graphics.size()) {
                    d.graphics[selection].translate(delta);
                }
            });
            m_dragLast = snapped;
        }
        return;
    }

    if (m_dragging) {
        m_pan += event->position() - m_dragLast;
        m_dragLast = event->position();
        update();
        return;
    }

    if (!m_pending.isEmpty() || m_tool != Tool::Select)
        update();
}

void SymbolCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_dragging = false;
    m_commands.breakMergeChain();
    setCursor(m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void SymbolCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (m_tool == Tool::Polyline && m_pending.size() >= 2)
        commitPending();
}

void SymbolCanvas::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (fuzzyZero(steps))
        return;
    const QPointF anchor = event->position();
    const QPointF sceneAnchor = toScene(anchor);
    m_scale = std::clamp(m_scale * std::pow(1.2, steps), 1.0, 80.0);
    m_pan = anchor - sceneAnchor * m_scale;
    update();
}

void SymbolCanvas::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        if (!m_pending.isEmpty()) {
            m_pending.clear();
            update();
        } else {
            setTool(Tool::Select);
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_tool == Tool::Polyline && m_pending.size() >= 2)
            commitPending();
        return;
    case Qt::Key_Delete:
        deleteSelection();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void SymbolCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_fitPending)
        zoomToFit();
    update();
}

void SymbolCanvas::paintPending(QPainter &painter) const
{
    if (m_pending.isEmpty())
        return;

    QPen pen(m_style.selection);
    pen.setWidthF(0.25);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF target = snap(m_cursor);
    switch (m_tool) {
    case Tool::Rect:
        painter.drawRect(normalized(m_pending.first(), target));
        break;
    case Tool::Circle:
    case Tool::Arc: {
        const QPointF centre = m_pending.first();
        const double radius = std::hypot(target.x() - centre.x(), target.y() - centre.y());
        painter.drawEllipse(centre, radius, radius);
        break;
    }
    case Tool::Polyline: {
        QVector<QPointF> preview = m_pending;
        preview.append(target);
        painter.drawPolyline(preview.constData(), int(preview.size()));
        break;
    }
    default:
        painter.drawLine(m_pending.first(), target);
        break;
    }
}

void SymbolCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), m_style.sheet);

    painter.translate(m_pan);
    painter.scale(m_scale, m_scale);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Grille et axes. L'origine est le point d'insertion du symbole : la voir
    // evite de dessiner un symbole qui se pose de travers.
    const QRectF view(toScene(QPointF(0, 0)), toScene(QPointF(width(), height())));
    if (m_gridStep > 0.0) {
        QPen gridPen(m_style.grid);
        gridPen.setWidthF(0.06);
        painter.setPen(gridPen);
        const int firstX = int(std::floor(view.left() / m_gridStep));
        const int lastX = int(std::ceil(view.right() / m_gridStep));
        const int firstY = int(std::floor(view.top() / m_gridStep));
        const int lastY = int(std::ceil(view.bottom() / m_gridStep));
        if (qint64(lastX - firstX + 1) * qint64(lastY - firstY + 1) < 40000) {
            for (int ix = firstX; ix <= lastX; ++ix)
                for (int iy = firstY; iy <= lastY; ++iy)
                    painter.drawPoint(QPointF(ix * m_gridStep, iy * m_gridStep));
        }
    }

    QPen axisPen(m_style.gridMajor);
    axisPen.setWidthF(0.12);
    painter.setPen(axisPen);
    painter.drawLine(QPointF(view.left(), 0), QPointF(view.right(), 0));
    painter.drawLine(QPointF(0, view.top()), QPointF(0, view.bottom()));

    // Le symbole lui-meme, trace par le moteur de rendu du logiciel : ce qu'on
    // dessine ici est exactement ce qui apparaitra sur un folio.
    FolioPainter::paintDefinition(painter, m_definition, m_style);

    // Reperes des broches : numero et point de connexion.
    QPen pinPen(m_style.pinMarker);
    pinPen.setWidthF(0.15);
    painter.setPen(pinPen);
    for (const Pin &pin : m_definition.pins) {
        painter.drawEllipse(pin.position, 0.5, 0.5);
        FolioPainter::drawTextMm(painter, pin.position + QPointF(0.9, -0.9), pin.number, 1.4);
    }

    // Selection.
    if (m_selection != kNoSelection) {
        QPen selectionPen(m_style.selection);
        selectionPen.setWidthF(0.2);
        selectionPen.setStyle(Qt::DashLine);
        painter.setPen(selectionPen);
        painter.setBrush(Qt::NoBrush);
        if (isPinSelection(m_selection)) {
            const int index = pinIndexOf(m_selection);
            if (index >= 0 && index < m_definition.pins.size()) {
                const Pin &pin = m_definition.pins.at(index);
                painter.drawRect(normalized(pin.root(), pin.position)
                                         .adjusted(-0.8, -0.8, 0.8, 0.8));
            }
        } else if (m_selection >= 0 && m_selection < m_definition.graphics.size()) {
            painter.drawRect(m_definition.graphics.at(m_selection)
                                     .bounds()
                                     .adjusted(-0.6, -0.6, 0.6, 0.6));
        }
    }

    paintPending(painter);
}

// ==========================================================================
// SymbolEditor

SymbolEditor::SymbolEditor(SymbolLibrary *library, QWidget *parent)
    : QDialog(parent), m_library(library)
{
    setWindowTitle(tr("Éditeur de symboles"));
    resize(1320, 820);

    auto *layout = new QVBoxLayout(this);

    auto *toolBar = new QToolBar(this);
    struct ToolSpec {
        SymbolCanvas::Tool tool;
        const char *label;
    };
    const ToolSpec tools[] = {
        { SymbolCanvas::Tool::Select, QT_TR_NOOP("Sélection") },
        { SymbolCanvas::Tool::Line, QT_TR_NOOP("Ligne") },
        { SymbolCanvas::Tool::Polyline, QT_TR_NOOP("Polyligne") },
        { SymbolCanvas::Tool::Rect, QT_TR_NOOP("Rectangle") },
        { SymbolCanvas::Tool::Circle, QT_TR_NOOP("Cercle") },
        { SymbolCanvas::Tool::Arc, QT_TR_NOOP("Arc") },
        { SymbolCanvas::Tool::Text, QT_TR_NOOP("Texte") },
        { SymbolCanvas::Tool::Pin, QT_TR_NOOP("Broche") },
    };

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_canvas = new SymbolCanvas(splitter);

    for (const ToolSpec &spec : tools) {
        auto *action = toolBar->addAction(tr(spec.label));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this,
                [this, spec, toolBar] {
                    m_canvas->setTool(spec.tool);
                    for (QAction *other : toolBar->actions())
                        other->setChecked(false);
                });
        connect(m_canvas, &SymbolCanvas::toolChanged, action,
                [action, spec](SymbolCanvas::Tool tool) { action->setChecked(tool == spec.tool); });
    }
    toolBar->addSeparator();
    connect(toolBar->addAction(tr("Annuler")), &QAction::triggered, this, [this] {
        m_canvas->commands().undo();
        m_canvas->update();
        rebuildElementList();
    });
    connect(toolBar->addAction(tr("Rétablir")), &QAction::triggered, this, [this] {
        m_canvas->commands().redo();
        m_canvas->update();
        rebuildElementList();
    });
    connect(toolBar->addAction(tr("Ajuster")), &QAction::triggered, m_canvas,
            &SymbolCanvas::zoomToFit);
    layout->addWidget(toolBar);

    // Panneau de gauche : metadonnees et liste des elements.
    auto *left = new QWidget(splitter);
    // Les noms de symboles et les identifiants sont longs : une colonne
    // etroite les tronque des l'ouverture.
    left->setMinimumWidth(320);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto *metadata = new QGroupBox(tr("Identité du symbole"), left);
    auto *form = new QFormLayout(metadata);
    buildMetadataForm(form);
    leftLayout->addWidget(metadata);

    auto *elementsBox = new QGroupBox(tr("Éléments"), left);
    auto *elementsLayout = new QVBoxLayout(elementsBox);
    m_elements = new QListWidget(elementsBox);
    elementsLayout->addWidget(m_elements);
    leftLayout->addWidget(elementsBox, 1);

    splitter->addWidget(left);
    splitter->addWidget(m_canvas);

    m_propertyScroll = new QScrollArea(splitter);
    m_propertyScroll->setWidgetResizable(true);
    m_propertyScroll->setMinimumWidth(240);
    splitter->addWidget(m_propertyScroll);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    layout->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    // Les libelles standard de Qt arrivent en anglais tant qu'aucune
    // traduction n'est chargee : on les pose explicitement.
    buttons->button(QDialogButtonBox::Save)->setText(tr("Enregistrer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SymbolEditor::saveDefinition);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_canvas, &SymbolCanvas::definitionChanged, this, [this] {
        if (!m_updating)
            rebuildElementList();
    });
    connect(m_canvas, &SymbolCanvas::selectionChanged, this, [this](int index) {
        m_updating = true;
        m_elements->setCurrentRow(-1);
        for (int row = 0; row < m_elements->count(); ++row) {
            if (m_elements->item(row)->data(Qt::UserRole).toInt() == index) {
                m_elements->setCurrentRow(row);
                break;
            }
        }
        m_updating = false;
        rebuildPropertyForm();
    });
    connect(m_elements, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!m_updating && item)
            m_canvas->setSelection(item->data(Qt::UserRole).toInt());
    });

    newDefinition();
}

void SymbolEditor::buildMetadataForm(QFormLayout *form)
{
    QWidget *parent = form->parentWidget();
    m_name = new QLineEdit(parent);
    m_logicalId = new QLineEdit(parent);
    m_logicalId->setPlaceholderText(QStringLiteral("contact-no"));
    m_norm = new QComboBox(parent);
    m_norm->addItems({ QStringLiteral("IEC"), QStringLiteral("ANSI") });
    m_category = new QLineEdit(parent);
    m_prefix = new QLineEdit(parent);
    m_prefix->setPlaceholderText(QStringLiteral("K"));
    m_deviceKind = new QLineEdit(parent);
    m_keywords = new QLineEdit(parent);
    m_keywords->setPlaceholderText(tr("séparés par des virgules"));

    form->addRow(tr("Nom"), m_name);
    form->addRow(tr("Identifiant"), m_logicalId);
    form->addRow(tr("Norme"), m_norm);
    form->addRow(tr("Catégorie"), m_category);
    form->addRow(tr("Préfixe"), m_prefix);
    form->addRow(tr("Famille"), m_deviceKind);
    form->addRow(tr("Mots-clés"), m_keywords);

    // L'identifiant logique est la cle qui relie les variantes CEI et ANSI du
    // meme symbole : deux symboles qui le partagent deviennent commutables.
    m_logicalId->setToolTip(tr("Deux symboles qui partagent cet identifiant deviennent les "
                               "variantes CEI et ANSI l'un de l'autre."));
}

void SymbolEditor::newDefinition()
{
    SymbolDefinition definition;
    definition.norm = QStringLiteral("IEC");
    definition.name = tr("Nouveau symbole");
    definition.logicalId = QStringLiteral("nouveau-symbole");
    definition.category = tr("Personnalisé");
    definition.id = SymbolDefinition::makeId(definition.norm, definition.logicalId);
    m_canvas->setDefinition(definition);

    m_name->setText(definition.name);
    m_logicalId->setText(definition.logicalId);
    m_norm->setCurrentText(definition.norm);
    m_category->setText(definition.category);
    m_prefix->clear();
    m_deviceKind->clear();
    m_keywords->clear();
    rebuildElementList();
}

void SymbolEditor::editDefinition(const QString &definitionId, bool asCopy)
{
    const SymbolDefinition *source = m_library ? m_library->definition(definitionId) : nullptr;
    if (!source) {
        newDefinition();
        return;
    }

    SymbolDefinition definition = *source;
    if (asCopy) {
        // Un symbole integre est duplique plutot que modifie en place : la
        // bibliotheque livree doit rester reproductible d'une version a l'autre.
        definition.logicalId += QStringLiteral("-perso");
        definition.name = tr("%1 (copie)").arg(definition.name);
        definition.id = SymbolDefinition::makeId(definition.norm, definition.logicalId);
    }
    m_canvas->setDefinition(definition);

    m_name->setText(definition.name);
    m_logicalId->setText(definition.logicalId);
    m_norm->setCurrentText(definition.norm);
    m_category->setText(definition.category);
    m_prefix->setText(definition.designationPrefix);
    m_deviceKind->setText(definition.deviceKind);
    m_keywords->setText(definition.keywords.join(QStringLiteral(", ")));
    rebuildElementList();
}

void SymbolEditor::rebuildElementList()
{
    m_updating = true;
    m_elements->clear();
    const SymbolDefinition &definition = m_canvas->definition();

    for (int i = 0; i < definition.pins.size(); ++i) {
        const Pin &pin = definition.pins.at(i);
        auto *item = new QListWidgetItem(tr("Broche %1").arg(pin.number), m_elements);
        item->setData(Qt::UserRole, selectionForPin(i));
    }
    for (int i = 0; i < definition.graphics.size(); ++i) {
        const Primitive &primitive = definition.graphics.at(i);
        auto *item = new QListWidgetItem(primitiveLabel(primitive), m_elements);
        item->setData(Qt::UserRole, i);
    }
    m_updating = false;
    rebuildPropertyForm();
}

void SymbolEditor::rebuildPropertyForm()
{
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    const SymbolDefinition &definition = m_canvas->definition();
    const int selection = m_canvas->selection();

    if (selection == SymbolCanvas::kNoSelection) {
        form->addRow(new QLabel(tr("Sélectionnez un élément."), content));
        m_propertyScroll->setWidget(content);
        return;
    }

    if (isPinSelection(selection)) {
        const int index = pinIndexOf(selection);
        if (index < 0 || index >= definition.pins.size()) {
            m_propertyScroll->setWidget(content);
            return;
        }
        const Pin pin = definition.pins.at(index);

        auto *number = new QLineEdit(pin.number, content);
        form->addRow(tr("Repère"), number);
        connect(number, &QLineEdit::editingFinished, this, [this, index, number] {
            m_canvas->modify(tr("Renommer une broche"), [index, number](SymbolDefinition &d) {
                if (index < d.pins.size())
                    d.pins[index].number = number->text();
            });
            rebuildElementList();
        });

        auto *name = new QLineEdit(pin.name, content);
        form->addRow(tr("Nom"), name);
        connect(name, &QLineEdit::editingFinished, this, [this, index, name] {
            m_canvas->modify(tr("Nommer une broche"), [index, name](SymbolDefinition &d) {
                if (index < d.pins.size())
                    d.pins[index].name = name->text();
            });
        });

        auto *direction = new QComboBox(content);
        direction->addItem(tr("Droite"), int(Direction::Right));
        direction->addItem(tr("Bas"), int(Direction::Down));
        direction->addItem(tr("Gauche"), int(Direction::Left));
        direction->addItem(tr("Haut"), int(Direction::Up));
        direction->setCurrentIndex(std::max(0, direction->findData(toDegrees(pin.direction))));
        form->addRow(tr("Sens"), direction);
        connect(direction, &QComboBox::currentIndexChanged, this, [this, index, direction] {
            const auto value = directionFromDegrees(direction->currentData().toInt());
            m_canvas->modify(tr("Orienter une broche"), [index, value](SymbolDefinition &d) {
                if (index < d.pins.size())
                    d.pins[index].direction = value;
            });
        });

        auto *length = new QDoubleSpinBox(content);
        length->setRange(0.0, 50.0);
        length->setSingleStep(1.25);
        length->setSuffix(QStringLiteral(" mm"));
        length->setValue(pin.length);
        form->addRow(tr("Longueur"), length);
        connect(length, &QDoubleSpinBox::valueChanged, this, [this, index](double value) {
            m_canvas->modify(tr("Régler une broche"), [index, value](SymbolDefinition &d) {
                if (index < d.pins.size())
                    d.pins[index].length = value;
            });
        });

        auto *type = new QComboBox(content);
        const QVector<QPair<PinType, QString>> types{
            { PinType::Passive, tr("Passive") },       { PinType::Input, tr("Entrée") },
            { PinType::Output, tr("Sortie") },         { PinType::Bidirectional, tr("Bidirectionnelle") },
            { PinType::Power, tr("Puissance") },       { PinType::Ground, tr("Terre") },
            { PinType::Terminal, tr("Borne") },        { PinType::NotConnected, tr("Non raccordée") },
        };
        for (const auto &entry : types)
            type->addItem(entry.second, int(entry.first));
        type->setCurrentIndex(std::max(0, type->findData(int(pin.type))));
        form->addRow(tr("Type"), type);
        connect(type, &QComboBox::currentIndexChanged, this, [this, index, type] {
            const auto value = PinType(type->currentData().toInt());
            m_canvas->modify(tr("Typer une broche"), [index, value](SymbolDefinition &d) {
                if (index < d.pins.size())
                    d.pins[index].type = value;
            });
        });

        auto *showNumber = new QCheckBox(tr("Afficher le repère"), content);
        showNumber->setChecked(pin.showNumber);
        form->addRow(QString(), showNumber);
        connect(showNumber, &QCheckBox::toggled, this, [this, index](bool on) {
            m_canvas->modify(tr("Affichage du repère"), [index, on](SymbolDefinition &d) {
                if (index < d.pins.size())
                    d.pins[index].showNumber = on;
            });
        });

        m_propertyScroll->setWidget(content);
        return;
    }

    if (selection < 0 || selection >= definition.graphics.size()) {
        m_propertyScroll->setWidget(content);
        return;
    }
    const Primitive primitive = definition.graphics.at(selection);
    form->addRow(tr("Type"), new QLabel(primitiveLabel(primitive), content));

    auto *width = new QDoubleSpinBox(content);
    width->setRange(0.0, 5.0);
    width->setDecimals(2);
    width->setSingleStep(0.05);
    width->setSuffix(QStringLiteral(" mm"));
    width->setValue(primitive.lineWidth);
    form->addRow(tr("Épaisseur"), width);
    connect(width, &QDoubleSpinBox::valueChanged, this, [this, selection](double value) {
        m_canvas->modify(tr("Épaisseur de trait"), [selection, value](SymbolDefinition &d) {
            if (selection < d.graphics.size())
                d.graphics[selection].lineWidth = value;
        });
    });

    if (primitive.kind == Primitive::Kind::Circle || primitive.kind == Primitive::Kind::Arc) {
        auto *radius = new QDoubleSpinBox(content);
        radius->setRange(0.1, 200.0);
        radius->setSuffix(QStringLiteral(" mm"));
        radius->setValue(primitive.radius);
        form->addRow(tr("Rayon"), radius);
        connect(radius, &QDoubleSpinBox::valueChanged, this, [this, selection](double value) {
            m_canvas->modify(tr("Rayon"), [selection, value](SymbolDefinition &d) {
                if (selection < d.graphics.size())
                    d.graphics[selection].radius = value;
            });
        });
    }

    if (primitive.kind == Primitive::Kind::Arc) {
        auto *start = new QDoubleSpinBox(content);
        start->setRange(-360.0, 360.0);
        start->setSuffix(QStringLiteral(" °"));
        start->setValue(primitive.startAngle);
        form->addRow(tr("Angle de départ"), start);
        connect(start, &QDoubleSpinBox::valueChanged, this, [this, selection](double value) {
            m_canvas->modify(tr("Angle de départ"), [selection, value](SymbolDefinition &d) {
                if (selection < d.graphics.size())
                    d.graphics[selection].startAngle = value;
            });
        });

        auto *span = new QDoubleSpinBox(content);
        span->setRange(-360.0, 360.0);
        span->setSuffix(QStringLiteral(" °"));
        span->setValue(primitive.spanAngle);
        form->addRow(tr("Ouverture"), span);
        connect(span, &QDoubleSpinBox::valueChanged, this, [this, selection](double value) {
            m_canvas->modify(tr("Ouverture"), [selection, value](SymbolDefinition &d) {
                if (selection < d.graphics.size())
                    d.graphics[selection].spanAngle = value;
            });
        });
    }

    if (primitive.kind == Primitive::Kind::Text) {
        auto *text = new QLineEdit(primitive.text, content);
        form->addRow(tr("Contenu"), text);
        connect(text, &QLineEdit::editingFinished, this, [this, selection, text] {
            m_canvas->modify(tr("Modifier un texte"), [selection, text](SymbolDefinition &d) {
                if (selection < d.graphics.size())
                    d.graphics[selection].text = text->text();
            });
            rebuildElementList();
        });

        auto *height = new QDoubleSpinBox(content);
        height->setRange(0.5, 30.0);
        height->setSuffix(QStringLiteral(" mm"));
        height->setValue(primitive.textHeight);
        form->addRow(tr("Hauteur"), height);
        connect(height, &QDoubleSpinBox::valueChanged, this, [this, selection](double value) {
            m_canvas->modify(tr("Hauteur du texte"), [selection, value](SymbolDefinition &d) {
                if (selection < d.graphics.size())
                    d.graphics[selection].textHeight = value;
            });
        });
    }

    auto *filled = new QCheckBox(tr("Rempli"), content);
    filled->setChecked(primitive.filled);
    form->addRow(QString(), filled);
    connect(filled, &QCheckBox::toggled, this, [this, selection](bool on) {
        m_canvas->modify(tr("Remplissage"), [selection, on](SymbolDefinition &d) {
            if (selection < d.graphics.size())
                d.graphics[selection].filled = on;
        });
    });

    m_propertyScroll->setWidget(content);
}

void SymbolEditor::applyMetadata()
{
    m_canvas->modify(tr("Identité du symbole"), [this](SymbolDefinition &d) {
        d.name = m_name->text().trimmed();
        d.logicalId = m_logicalId->text().trimmed();
        d.norm = m_norm->currentText();
        d.category = m_category->text().trimmed();
        d.designationPrefix = m_prefix->text().trimmed();
        d.deviceKind = m_deviceKind->text().trimmed();
        d.keywords.clear();
        const QStringList parts =
                m_keywords->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts)
            d.keywords.append(part.trimmed());
        d.id = SymbolDefinition::makeId(d.norm, d.logicalId);
    });
}

void SymbolEditor::saveDefinition()
{
    applyMetadata();
    const SymbolDefinition &definition = m_canvas->definition();

    if (definition.logicalId.isEmpty() || definition.name.isEmpty()) {
        QMessageBox::warning(this, tr("Symbole incomplet"),
                             tr("Un symbole a besoin d'un nom et d'un identifiant."));
        return;
    }
    if (definition.pins.isEmpty()) {
        // Un symbole sans broche ne peut pas etre raccorde : il ne sert a rien
        // sur un schema, et laisser l'utilisateur en enregistrer un lui ferait
        // decouvrir le probleme bien plus tard.
        const auto answer = QMessageBox::question(
                this, tr("Symbole sans broche"),
                tr("Ce symbole n'a aucune broche : il ne pourra jamais être raccordé à un "
                   "potentiel. L'enregistrer quand même ?"));
        if (answer != QMessageBox::Yes)
            return;
    }

    const QString directory = LibraryStore::writableUserPath();
    if (directory.isEmpty()) {
        QMessageBox::critical(this, tr("Enregistrement impossible"),
                              tr("Aucun dossier utilisateur accessible en écriture."));
        return;
    }

    QString category = definition.category.isEmpty() ? QStringLiteral("personnalise")
                                                     : definition.category.toLower();
    category.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    const QString path = QStringLiteral("%1/%2/%3.json")
                                 .arg(directory, definition.norm.toLower(), category);

    // Les symboles de la meme categorie vivent dans un fichier : on relit
    // l'existant pour ne pas ecraser les voisins.
    SymbolLibrary file;
    LibraryStore::loadFile(path, file);
    file.insert(definition);

    QString error;
    if (!LibraryStore::saveFile(path, file.all(), &error)) {
        QMessageBox::critical(this, tr("Enregistrement impossible"), error);
        return;
    }

    if (m_library)
        m_library->insert(definition);
    m_savedId = definition.id;
    accept();
}

} // namespace dsn
