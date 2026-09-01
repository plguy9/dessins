#include "foliopainter.h"

#include "core/entities.h"

#include <QFontMetricsF>
#include <QPainterPath>

namespace dsn {

namespace {

// Taille de reference de la police. Le texte est trace a cette taille puis mis
// a l'echelle : la hauteur de capitale obtenue est alors exacte en
// millimetres, independamment du peripherique et du zoom.
constexpr int kFontReferencePixels = 128;

QRectF rectOf(const Primitive &primitive)
{
    if (primitive.points.size() < 2)
        return QRectF();
    return normalized(primitive.points.at(0), primitive.points.at(1));
}

} // namespace

FolioPainter::FolioPainter(const Project &project, RenderStyle style)
    : m_project(project), m_style(std::move(style))
{
}

QPen FolioPainter::pen(const QColor &color, double width) const
{
    QPen p(color);
    p.setWidthF(width);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    // Un trait cosmetique disparaitrait au zoom arriere : l'epaisseur doit
    // rester une grandeur du dessin, en millimetres.
    p.setCosmetic(false);
    return p;
}

void FolioPainter::drawTextMm(QPainter &painter, const QPointF &at, const QString &text,
                              double heightMm, Primitive::Align align, double rotationDegrees)
{
    if (text.isEmpty() || heightMm <= 0.0)
        return;

    QFont font = painter.font();
    font.setPixelSize(kFontReferencePixels);
    const QFontMetricsF metrics(font);
    const double capHeight = metrics.capHeight() > 0.0 ? metrics.capHeight() : metrics.ascent();
    if (capHeight <= 0.0)
        return;
    const double scale = heightMm / capHeight;

    double dx = 0.0;
    const double advance = metrics.horizontalAdvance(text);
    if (align == Primitive::Align::Center)
        dx = -advance / 2.0;
    else if (align == Primitive::Align::Right)
        dx = -advance;

    painter.save();
    painter.translate(at);
    if (!fuzzyZero(rotationDegrees))
        painter.rotate(rotationDegrees);
    painter.scale(scale, scale);
    painter.setFont(font);
    painter.drawText(QPointF(dx, 0.0), text);
    painter.restore();
}

QRectF FolioPainter::textBoundsMm(const QPainter &painter, const QPointF &at, const QString &text,
                                  double heightMm, Primitive::Align align)
{
    QFont font = painter.font();
    font.setPixelSize(kFontReferencePixels);
    const QFontMetricsF metrics(font);
    const double capHeight = metrics.capHeight() > 0.0 ? metrics.capHeight() : metrics.ascent();
    if (capHeight <= 0.0)
        return QRectF();
    const double scale = heightMm / capHeight;
    const double width = metrics.horizontalAdvance(text) * scale;
    const double descent = metrics.descent() * scale;

    double left = at.x();
    if (align == Primitive::Align::Center)
        left -= width / 2.0;
    else if (align == Primitive::Align::Right)
        left -= width;
    return QRectF(left, at.y() - heightMm, width, heightMm + descent);
}

void FolioPainter::paintPrimitive(QPainter &painter, const Primitive &primitive)
{
    switch (primitive.kind) {
    case Primitive::Kind::Line:
        if (primitive.points.size() >= 2)
            painter.drawLine(primitive.points.at(0), primitive.points.at(1));
        break;
    case Primitive::Kind::Polyline:
        if (primitive.points.size() >= 2)
            painter.drawPolyline(primitive.points.constData(), int(primitive.points.size()));
        break;
    case Primitive::Kind::Rect: {
        const QRectF r = rectOf(primitive);
        if (primitive.filled)
            painter.fillRect(r, painter.pen().color());
        painter.drawRect(r);
        break;
    }
    case Primitive::Kind::Circle: {
        if (primitive.points.isEmpty())
            break;
        const QPointF c = primitive.points.first();
        const QRectF r(c.x() - primitive.radius, c.y() - primitive.radius, primitive.radius * 2.0,
                       primitive.radius * 2.0);
        if (primitive.filled) {
            painter.save();
            painter.setBrush(painter.pen().color());
            painter.drawEllipse(r);
            painter.restore();
        } else {
            painter.drawEllipse(r);
        }
        break;
    }
    case Primitive::Kind::Arc: {
        if (primitive.points.isEmpty())
            break;
        const QPointF c = primitive.points.first();
        const QRectF r(c.x() - primitive.radius, c.y() - primitive.radius, primitive.radius * 2.0,
                       primitive.radius * 2.0);
        // Qt compte les angles en seiziemes de degre, sens trigonometrique,
        // origine a trois heures : c'est exactement la convention de Primitive.
        painter.drawArc(r, int(primitive.startAngle * 16), int(primitive.spanAngle * 16));
        break;
    }
    case Primitive::Kind::Text:
        if (!primitive.points.isEmpty())
            drawTextMm(painter, primitive.points.first(), primitive.text, primitive.textHeight,
                       primitive.align);
        break;
    }
}


void FolioPainter::paintSnapMarker(QPainter &painter, SnapMode mode, const QPointF &point,
                                   double sizeMm, const QColor &color)
{
    const double h = sizeMm / 2.0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    // Le marqueur est un peu plus epais que le dessin : il doit se lire
    // par-dessus un trait, pas se confondre avec lui.
    pen.setWidthF(sizeMm * 0.11);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (mode) {
    case SnapMode::Endpoint:
        painter.drawRect(QRectF(point.x() - h, point.y() - h, sizeMm, sizeMm));
        break;

    case SnapMode::Midpoint:
        // Triangle pointe en haut.
        painter.drawPolygon(QPolygonF({ QPointF(point.x(), point.y() - h),
                                        QPointF(point.x() + h, point.y() + h),
                                        QPointF(point.x() - h, point.y() + h) }));
        break;

    case SnapMode::Center:
        painter.drawEllipse(point, h, h);
        break;

    case SnapMode::Node:
        // Cercle barre d'une croix : le point de raccordement electrique.
        painter.drawEllipse(point, h, h);
        painter.drawLine(point + QPointF(-h, -h) * 0.72, point + QPointF(h, h) * 0.72);
        painter.drawLine(point + QPointF(-h, h) * 0.72, point + QPointF(h, -h) * 0.72);
        break;

    case SnapMode::Quadrant:
        painter.drawPolygon(QPolygonF({ QPointF(point.x(), point.y() - h),
                                        QPointF(point.x() + h, point.y()),
                                        QPointF(point.x(), point.y() + h),
                                        QPointF(point.x() - h, point.y()) }));
        break;

    case SnapMode::Intersection:
        painter.drawLine(point + QPointF(-h, -h), point + QPointF(h, h));
        painter.drawLine(point + QPointF(-h, h), point + QPointF(h, -h));
        break;

    case SnapMode::Perpendicular: {
        // L'equerre : deux cotes et le petit trait du coin droit.
        painter.drawLine(point + QPointF(-h, -h), point + QPointF(-h, h));
        painter.drawLine(point + QPointF(-h, h), point + QPointF(h, h));
        painter.drawLine(point + QPointF(-h, 0), point + QPointF(0, 0));
        painter.drawLine(point + QPointF(0, 0), point + QPointF(0, h));
        break;
    }

    case SnapMode::Nearest:
        // Le sablier d'AutoCAD : deux triangles opposes par la pointe.
        painter.drawPolygon(QPolygonF({ point + QPointF(-h, -h), point + QPointF(h, -h),
                                        point + QPointF(-h, h), point + QPointF(h, h) }));
        break;

    case SnapMode::Insertion:
        // Deux carres decales, comme le point d'insertion d'un bloc.
        painter.drawRect(QRectF(point.x() - h, point.y() - h, sizeMm * 0.78, sizeMm * 0.78));
        painter.drawRect(QRectF(point.x() - h + sizeMm * 0.22, point.y() - h + sizeMm * 0.22,
                                sizeMm * 0.78, sizeMm * 0.78));
        break;

    case SnapMode::Extension:
        // Une croix en plus, comme la marque de prolongement.
        painter.drawLine(point + QPointF(-h, 0), point + QPointF(h, 0));
        painter.drawLine(point + QPointF(0, -h), point + QPointF(0, h));
        break;

    case SnapMode::Grid:
        // La grille n'affiche pas de marqueur : elle accroche en permanence,
        // et un marqueur constant sous le curseur deviendrait du bruit.
        break;
    }
    painter.restore();
}

void FolioPainter::paintDefinition(QPainter &painter, const SymbolDefinition &definition,
                                   const RenderStyle &style, bool withPins)
{
    painter.save();
    for (const Primitive &primitive : definition.graphics) {
        QPen p(style.symbol);
        p.setWidthF(primitive.lineWidth > 0.0 ? primitive.lineWidth : style.symbolWidth);
        p.setCapStyle(Qt::RoundCap);
        p.setJoinStyle(Qt::RoundJoin);
        painter.setPen(p);
        painter.setBrush(Qt::NoBrush);
        paintPrimitive(painter, primitive);
    }

    if (withPins) {
        QPen p(style.symbol);
        p.setWidthF(style.symbolWidth);
        p.setCapStyle(Qt::RoundCap);
        painter.setPen(p);
        for (const Pin &pin : definition.pins) {
            if (pin.type == PinType::NotConnected)
                continue;
            painter.drawLine(pin.root(), pin.position);
        }
    }
    painter.restore();
}

QColor FolioPainter::colorFor(const Entity &entity, const QColor &base) const
{
    if (m_selection.contains(entity.id()))
        return m_style.selection;
    if (m_highlight.contains(entity.id()))
        return m_style.highlight;
    return base;
}

void FolioPainter::paintSheet(QPainter &painter, const Folio &folio) const
{
    const QRectF sheet = folio.sheetRect();
    if (m_style.showSheetShadow) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_style.sheetShadow);
        painter.drawRect(sheet.translated(1.5, 1.5));
        painter.restore();
    }
    painter.fillRect(sheet, m_style.sheet);
}

void FolioPainter::paintGrid(QPainter &painter, const Folio &folio, const QRectF &clipMm) const
{
    if (!m_style.showGrid || m_style.gridStep <= 0.0)
        return;

    QRectF area = clipMm.isNull() ? folio.sheetRect() : clipMm.intersected(folio.sheetRect());
    if (area.isEmpty())
        return;

    const double step = m_style.gridStep;
    const double major = step * std::max(1, m_style.gridMajorEvery);

    const int firstX = int(std::floor(area.left() / step));
    const int lastX = int(std::ceil(area.right() / step));
    const int firstY = int(std::floor(area.top() / step));
    const int lastY = int(std::ceil(area.bottom() / step));

    // Au-dela de quelques dizaines de milliers de points, la grille coute plus
    // cher que le dessin lui-meme : on l'abandonne plutot que de ramer.
    const qint64 dots = qint64(lastX - firstX + 1) * qint64(lastY - firstY + 1);
    if (dots > 40000)
        return;

    painter.save();
    QPen minor = pen(m_style.grid, m_style.gridDotWidth);
    QPen strong = pen(m_style.gridMajor, m_style.gridDotWidth * 1.6);
    for (int ix = firstX; ix <= lastX; ++ix) {
        for (int iy = firstY; iy <= lastY; ++iy) {
            const QPointF p(ix * step, iy * step);
            const bool isMajor = fuzzyZero(std::fmod(std::abs(p.x()), major), 1e-6)
                    && fuzzyZero(std::fmod(std::abs(p.y()), major), 1e-6);
            painter.setPen(isMajor ? strong : minor);
            painter.drawPoint(p);
        }
    }
    painter.restore();
}

void FolioPainter::paintFrame(QPainter &painter, const Folio &folio) const
{
    if (!m_style.showFrame)
        return;

    const QRectF frame = folio.frameRect();
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pen(m_style.frame, m_style.frameWidth));
    painter.drawRect(frame);

    if (m_style.showZoneLabels && folio.frame.columns > 0 && folio.frame.rows > 0) {
        const double columnWidth = frame.width() / folio.frame.columns;
        const double rowHeight = frame.height() / folio.frame.rows;
        painter.setPen(pen(m_style.frame, m_style.frameWidth * 0.4));

        for (int c = 1; c < folio.frame.columns; ++c) {
            const double x = frame.left() + c * columnWidth;
            painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.top() + 4.0));
            painter.drawLine(QPointF(x, frame.bottom() - 4.0), QPointF(x, frame.bottom()));
        }
        for (int r = 1; r < folio.frame.rows; ++r) {
            const double y = frame.top() + r * rowHeight;
            painter.drawLine(QPointF(frame.left(), y), QPointF(frame.left() + 4.0, y));
            painter.drawLine(QPointF(frame.right() - 4.0, y), QPointF(frame.right(), y));
        }

        painter.setPen(m_style.frame);
        for (int c = 1; c <= folio.frame.columns; ++c) {
            const double x = frame.left() + (c - 0.5) * columnWidth;
            drawTextMm(painter, QPointF(x, frame.top() - 1.6), QString::number(c), 2.2,
                       Primitive::Align::Center);
            drawTextMm(painter, QPointF(x, frame.bottom() + 3.6), QString::number(c), 2.2,
                       Primitive::Align::Center);
        }
        for (int r = 1; r <= folio.frame.rows; ++r) {
            const double y = frame.top() + (r - 0.5) * rowHeight + 1.1;
            const QString letter(QChar(char16_t(u'A' + r - 1)));
            drawTextMm(painter, QPointF(frame.left() - 3.0, y), letter, 2.2,
                       Primitive::Align::Center);
            drawTextMm(painter, QPointF(frame.right() + 3.0, y), letter, 2.2,
                       Primitive::Align::Center);
        }
    }
    painter.restore();
}

void FolioPainter::paintTitleBlock(QPainter &painter, const Folio &folio) const
{
    if (!m_style.showTitleBlock)
        return;

    const QRectF block = folio.titleBlockRect();
    if (block.width() < 40.0 || block.height() < 16.0)
        return;

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pen(m_style.frame, m_style.frameWidth));
    painter.drawRect(block);

    // Trois bandes : titre du projet, folio, mentions administratives.
    const double rowHeight = block.height() / 3.0;
    painter.setPen(pen(m_style.frame, m_style.frameWidth * 0.5));
    for (int i = 1; i < 3; ++i) {
        const double y = block.top() + i * rowHeight;
        painter.drawLine(QPointF(block.left(), y), QPointF(block.right(), y));
    }
    const double splitX = block.left() + block.width() * 0.62;
    painter.drawLine(QPointF(splitX, block.top() + rowHeight), QPointF(splitX, block.bottom()));

    const ProjectInfo &info = m_project.info;
    const double pad = 2.0;
    painter.setPen(m_style.text);

    drawTextMm(painter, QPointF(block.left() + pad, block.top() + rowHeight - 2.4),
               info.title.isEmpty() ? QStringLiteral("Projet sans titre") : info.title, 3.6);

    drawTextMm(painter, QPointF(block.left() + pad, block.top() + 2 * rowHeight - 5.0),
               folio.title, 2.6);
    drawTextMm(painter, QPointF(block.left() + pad, block.top() + 2 * rowHeight - 1.4),
               info.client, 2.2);

    drawTextMm(painter, QPointF(splitX + pad, block.top() + 2 * rowHeight - 5.0),
               QStringLiteral("Folio ") + folio.number, 2.6);
    drawTextMm(painter, QPointF(splitX + pad, block.top() + 2 * rowHeight - 1.4),
               QStringLiteral("Réf. ") + info.reference, 2.2);

    drawTextMm(painter, QPointF(block.left() + pad, block.bottom() - 4.4), info.author, 2.2);
    drawTextMm(painter, QPointF(block.left() + pad, block.bottom() - 1.2),
               info.date.isValid() ? info.date.toString(QStringLiteral("dd/MM/yyyy")) : QString(),
               2.2);

    drawTextMm(painter, QPointF(splitX + pad, block.bottom() - 4.4),
               QStringLiteral("Indice ") + info.revision, 2.2);
    drawTextMm(painter, QPointF(splitX + pad, block.bottom() - 1.2), folio.sheet.id, 2.2);

    painter.restore();
}

void FolioPainter::paintWire(QPainter &painter, const Wire &wire) const
{
    if (wire.points.size() < 2)
        return;

    const QColor color = colorFor(wire, m_style.wire);
    const int conductors = wire.conductorCount();

    painter.save();
    painter.setBrush(Qt::NoBrush);

    if (conductors <= 1) {
        painter.setPen(pen(color, m_style.wireWidth));
        painter.drawPolyline(wire.points.constData(), int(wire.points.size()));
    } else {
        // Representation unifilaire : un trait unique, barre d'autant de
        // marques obliques qu'il porte de conducteurs.
        painter.setPen(pen(color, m_style.wireWidth * 1.4));
        painter.drawPolyline(wire.points.constData(), int(wire.points.size()));

        const QPointF a = wire.points.at(0);
        const QPointF b = wire.points.at(1);
        QPointF direction = b - a;
        const double length = std::hypot(direction.x(), direction.y());
        if (length > 6.0) {
            direction /= length;
            const QPointF normal(-direction.y(), direction.x());
            const QPointF anchor = a + direction * (length * 0.5);
            painter.setPen(pen(color, m_style.wireWidth * 0.8));
            for (int i = 0; i < conductors; ++i) {
                const QPointF centre = anchor + direction * (i - (conductors - 1) / 2.0) * 1.6;
                painter.drawLine(centre - direction * 0.9 - normal * 1.6,
                                 centre + direction * 0.9 + normal * 1.6);
            }
        }
    }

    if (m_style.showWireNumbers && !wire.number.isEmpty()) {
        const QPointF a = wire.points.at(0);
        const QPointF b = wire.points.at(1);
        const bool vertical = std::abs(b.y() - a.y()) > std::abs(b.x() - a.x());
        const QPointF middle = (a + b) / 2.0;
        painter.setPen(m_style.tag);
        // Le repere se pose a cote du fil, jamais dessus : il doit rester
        // lisible sur un folio dense.
        const QPointF at = vertical ? middle + QPointF(1.2, -0.8) : middle + QPointF(0.0, -1.2);
        drawTextMm(painter, at, wire.number, m_style.wireNumberHeight,
                   vertical ? Primitive::Align::Left : Primitive::Align::Center);
    }
    painter.restore();
}

void FolioPainter::paintSymbol(QPainter &painter, const SymbolInstance &symbol) const
{
    const SymbolDefinition *definition = m_project.library.definition(symbol.definitionId);
    const QColor color = colorFor(symbol, m_style.symbol);

    const Transform2D t = symbol.placement.transform();
    const QTransform qt(t.m11, t.m12, t.m21, t.m22, t.dx, t.dy);

    painter.save();
    painter.setWorldTransform(qt, true);

    if (!definition) {
        // Symbole introuvable : on trace un cadre barre plutot que rien. Un
        // trou invisible dans un schema est bien pire qu'une marque explicite.
        painter.setPen(pen(m_style.highlight, m_style.symbolWidth));
        painter.setBrush(Qt::NoBrush);
        const QRectF box(-5, -5, 10, 10);
        painter.drawRect(box);
        painter.drawLine(box.topLeft(), box.bottomRight());
        painter.drawLine(box.bottomLeft(), box.topRight());
        painter.restore();
        return;
    }

    painter.setBrush(Qt::NoBrush);
    for (const Primitive &primitive : definition->graphics) {
        painter.setPen(pen(color, primitive.lineWidth > 0.0 ? primitive.lineWidth
                                                            : m_style.symbolWidth));
        paintPrimitive(painter, primitive);
    }

    painter.setPen(pen(color, m_style.symbolWidth));
    for (const Pin &pin : definition->pins) {
        if (pin.type == PinType::NotConnected)
            continue;
        painter.drawLine(pin.root(), pin.position);
    }

    if (m_style.showPinNumbers) {
        painter.setPen(m_style.tag);
        for (const Pin &pin : definition->pins) {
            if (!pin.showNumber || pin.number.isEmpty())
                continue;
            drawTextMm(painter, pin.root() + QPointF(0.8, -0.8), pin.number, 1.5);
        }
    }
    painter.restore();

    // Les textes attaches restent horizontaux quelle que soit la rotation du
    // symbole : un repere tete en bas est illisible sur un plan.
    painter.save();
    if (m_style.showDesignations) {
        const QString designation = symbol.designation();
        if (!designation.isEmpty()) {
            painter.setPen(m_selection.contains(symbol.id()) ? m_style.selection : m_style.tag);
            drawTextMm(painter, symbol.placement.map(definition->designationAnchor), designation,
                       m_style.designationHeight, Primitive::Align::Center);
        }
    }
    if (m_style.showValues) {
        const QString value = symbol.fields.value(QStringLiteral("value"));
        if (!value.isEmpty()) {
            painter.setPen(m_style.text);
            drawTextMm(painter, symbol.placement.map(definition->valueAnchor), value,
                       m_style.valueHeight, Primitive::Align::Center);
        }
    }
    painter.restore();
}

void FolioPainter::paintLabel(QPainter &painter, const Label &label) const
{
    const QColor color = colorFor(label, m_style.label);
    painter.save();
    painter.setPen(pen(color, m_style.symbolWidth));
    painter.setBrush(Qt::NoBrush);

    const QPointF direction = unitVector(label.direction);
    const QPointF tip = label.point;
    const double length = label.height * 0.62 * std::max(2, int(label.name.size())) + 3.0;
    const QPointF tail = tip + direction * length;
    const QPointF normal(-direction.y(), direction.x());
    const double h = label.height * 0.8;

    // Un renvoi de folio porte une pointe, une etiquette locale non : la
    // difference de portee doit se voir sur le dessin.
    QPolygonF outline;
    if (label.scope == Label::Scope::Project) {
        outline << tip << tip + direction * 2.0 + normal * h << tail + normal * h
                << tail - normal * h << tip + direction * 2.0 - normal * h;
    } else {
        outline << tip + normal * h << tail + normal * h << tail - normal * h << tip - normal * h;
    }
    painter.drawPolygon(outline);

    painter.setPen(color);
    drawTextMm(painter, (tip + tail) / 2.0 + QPointF(0.0, label.height * 0.35), label.name,
               label.height, Primitive::Align::Center);
    painter.restore();
}

void FolioPainter::paintEntity(QPainter &painter, const Entity &entity) const
{
    switch (entity.type()) {
    case EntityType::Wire:
        paintWire(painter, static_cast<const Wire &>(entity));
        break;
    case EntityType::Symbol:
        paintSymbol(painter, static_cast<const SymbolInstance &>(entity));
        break;
    case EntityType::Junction: {
        const auto &junction = static_cast<const Junction &>(entity);
        painter.save();
        const QColor color = colorFor(entity, m_style.wire);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(junction.point, junction.diameter / 2.0, junction.diameter / 2.0);
        painter.restore();
        break;
    }
    case EntityType::Text: {
        const auto &item = static_cast<const TextItem &>(entity);
        const auto align = item.align == TextItem::Align::Center ? Primitive::Align::Center
                         : item.align == TextItem::Align::Right  ? Primitive::Align::Right
                                                                 : Primitive::Align::Left;
        painter.save();
        painter.setPen(colorFor(entity, m_style.text));
        drawTextMm(painter, item.placement.position, item.text, item.height, align,
                   double(toDegrees(item.placement.orientation)));
        painter.restore();
        break;
    }
    case EntityType::Graphic: {
        const auto &item = static_cast<const GraphicItem &>(entity);
        painter.save();
        painter.setPen(pen(colorFor(entity, m_style.symbol),
                           item.shape.lineWidth > 0.0 ? item.shape.lineWidth
                                                      : m_style.symbolWidth));
        painter.setBrush(Qt::NoBrush);
        paintPrimitive(painter, item.shape);
        painter.restore();
        break;
    }
    case EntityType::Label:
        paintLabel(painter, static_cast<const Label &>(entity));
        break;
    }
}

void FolioPainter::paintEntities(QPainter &painter, const Folio &folio, const QRectF &clipMm) const
{
    for (const EntityPtr &entity : folio.entities()) {
        // Le rejet par boite englobante evite de parcourir tout le folio a
        // chaque rafraichissement d'une petite zone.
        if (!clipMm.isNull()) {
            const QRectF bounds = entity->boundingBox().adjusted(-6, -6, 6, 6);
            if (!bounds.isNull() && !clipMm.intersects(bounds))
                continue;
        }
        paintEntity(painter, *entity);
    }
}

void FolioPainter::paintDecorations(QPainter &painter, const Folio &folio) const
{
    Q_UNUSED(folio);
    if (!m_style.showUnconnectedPins || m_unconnected.isEmpty())
        return;

    painter.save();
    painter.setPen(pen(m_style.pinMarker, m_style.symbolWidth));
    painter.setBrush(Qt::NoBrush);
    for (const QPointF &p : m_unconnected) {
        const double r = 0.9;
        painter.drawLine(p + QPointF(-r, -r), p + QPointF(r, r));
        painter.drawLine(p + QPointF(-r, r), p + QPointF(r, -r));
    }
    painter.restore();
}

void FolioPainter::paint(QPainter &painter, const Folio &folio, const QRectF &clipMm) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font(m_style.fontFamily);
    font.setStyleStrategy(QFont::PreferAntialias);
    painter.setFont(font);

    paintSheet(painter, folio);
    paintGrid(painter, folio, clipMm);
    paintFrame(painter, folio);
    paintTitleBlock(painter, folio);
    paintEntities(painter, folio, clipMm);
    paintDecorations(painter, folio);
    painter.restore();
}

} // namespace dsn
