#include "foliopainter.h"

#include "core/titleblock.h"

#include "core/entities.h"

#include <QFontMetricsF>
#include <QPainterPath>

#include <algorithm>

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

void FolioPainter::applyStroke(QPen &pen, Primitive::Stroke stroke)
{
    if (stroke == Primitive::Stroke::Solid) {
        pen.setStyle(Qt::SolidLine);
        return;
    }
    // Le motif de Qt est exprime en multiples de l'epaisseur. On le divise
    // donc par l'epaisseur pour obtenir des tirets d'une longueur donnee EN
    // MILLIMETRES : un cadre d'armoire au trait fin et un autre au trait
    // epais doivent porter le meme pointille, sinon les deux ne se lisent
    // plus comme la meme convention.
    const double w = std::max(0.05, pen.widthF());
    const auto mm = [w](double millimetres) { return millimetres / w; };
    switch (stroke) {
    case Primitive::Stroke::Dashed:
        pen.setDashPattern({ mm(3.0), mm(2.0) });
        break;
    case Primitive::Stroke::Dotted:
        pen.setDashPattern({ mm(0.5), mm(1.5) });
        break;
    case Primitive::Stroke::DashDot:
        pen.setDashPattern({ mm(6.0), mm(2.0), mm(0.5), mm(2.0) });
        break;
    case Primitive::Stroke::Solid:
        break;
    }
    // Un bout arrondi ferme les tirets d'un demi-diametre de chaque cote et
    // les fait se rejoindre : le pointille disparait sur un trait epais.
    pen.setCapStyle(Qt::FlatCap);
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

double FolioPainter::textWidthMm(const QFont &base, const QString &text, double heightMm)
{
    if (text.isEmpty() || heightMm <= 0.0)
        return 0.0;
    QFont font = base;
    font.setPixelSize(kFontReferencePixels);
    const QFontMetricsF metrics(font);
    const double capHeight = metrics.capHeight() > 0.0 ? metrics.capHeight() : metrics.ascent();
    if (capHeight <= 0.0)
        return 0.0;
    return metrics.horizontalAdvance(text) * (heightMm / capHeight);
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
        applyStroke(p, primitive.stroke);
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

QColor FolioPainter::wireTypeColor(const WireType &type)
{
    return QColor(QRgb(0xFF000000u | (type.rgb & 0x00FFFFFFu)));
}

Qt::PenStyle FolioPainter::wireTypePenStyle(const WireType &type)
{
    if (type.style == QLatin1String("dashed"))
        return Qt::DashLine;
    if (type.style == QLatin1String("dotted"))
        return Qt::DotLine;
    if (type.style == QLatin1String("dashdot"))
        return Qt::DashDotLine;
    return Qt::SolidLine;
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

double FolioPainter::displayGridStep(double nominalStep, double pixelsPerMm)
{
    if (nominalStep <= 0.0)
        return nominalStep;
    // Echelle inconnue (PDF, vignette) : on garde le pas nominal.
    if (pixelsPerMm <= 0.0)
        return nominalStep;

    double step = nominalStep;
    // On DOUBLE, on ne multiplie pas par un facteur quelconque : chaque marque
    // tracee reste ainsi sur le pas nominal, donc sur un point d'accrochage.
    // Un facteur 1,5 mettrait une marque sur deux entre deux points.
    int doublements = 0;
    while (step * pixelsPerMm < kMinGridPixels && doublements < 16) {
        step *= 2.0;
        ++doublements;
    }
    return step;
}

void FolioPainter::paintGrid(QPainter &painter, const Folio &folio, const QRectF &clipMm) const
{
    if (!m_style.showGrid || m_style.gridStep <= 0.0)
        return;

    QRectF area = clipMm.isNull() ? folio.sheetRect() : clipMm.intersected(folio.sheetRect());
    if (area.isEmpty())
        return;

    const double step = displayGridStep(m_style.gridStep, m_style.pixelsPerMm);
    const double major = step * std::max(1, m_style.gridMajorEvery);

    const int firstX = int(std::floor(area.left() / step));
    const int lastX = int(std::ceil(area.right() / step));
    const int firstY = int(std::floor(area.top() / step));
    const int lastY = int(std::ceil(area.bottom() / step));

    // Au-dela de quelques dizaines de milliers de marques, la grille coute plus
    // cher que le dessin lui-meme : on l'abandonne plutot que de ramer.
    //
    // Le compte depend de l'aspect, et c'est la tout l'interet de le calculer
    // ici : les carreaux coutent la SOMME des deux directions, les points leur
    // PRODUIT. Un seul garde-fou calibre sur les points ferait disparaitre des
    // carreaux qu'on trace sans effort — quelques centaines de traits la ou il
    // y aurait eu cent mille points.
    const qint64 columns = lastX - firstX + 1;
    const qint64 rows = lastY - firstY + 1;
    const qint64 marks = m_style.gridStyle == GridStyle::Lines ? columns + rows
                                                               : columns * rows;
    if (marks > 40000)
        return;

    painter.save();
    const QPen minor = pen(m_style.grid, m_style.gridDotWidth);
    const QPen strong = pen(m_style.gridMajor, m_style.gridDotWidth * 1.6);

    // Les carreaux se tracent ligne par ligne et non point par point : une
    // ligne continue est un seul trait pour le peintre, la ou la grille de
    // points en dessine des milliers.
    if (m_style.gridStyle == GridStyle::Lines) {
        for (int ix = firstX; ix <= lastX; ++ix) {
            const double x = ix * step;
            painter.setPen(fuzzyZero(std::fmod(std::abs(x), major), 1e-6) ? strong : minor);
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
        for (int iy = firstY; iy <= lastY; ++iy) {
            const double y = iy * step;
            painter.setPen(fuzzyZero(std::fmod(std::abs(y), major), 1e-6) ? strong : minor);
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
        painter.restore();
        return;
    }

    const double arm = std::max(0.1, m_style.gridCrossSize);
    for (int ix = firstX; ix <= lastX; ++ix) {
        for (int iy = firstY; iy <= lastY; ++iy) {
            const QPointF p(ix * step, iy * step);
            const bool isMajor = fuzzyZero(std::fmod(std::abs(p.x()), major), 1e-6)
                    && fuzzyZero(std::fmod(std::abs(p.y()), major), 1e-6);
            painter.setPen(isMajor ? strong : minor);
            if (m_style.gridStyle == GridStyle::Crosses) {
                painter.drawLine(QPointF(p.x() - arm, p.y()), QPointF(p.x() + arm, p.y()));
                painter.drawLine(QPointF(p.x(), p.y() - arm), QPointF(p.x(), p.y() + arm));
            } else {
                painter.drawPoint(p);
            }
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

        // Les etiquettes suivent le SENS du reperage. Certains bureaux comptent
        // les colonnes de droite a gauche et les lignes du bas vers le haut —
        // la zone A du cote du cartouche. Ecrire l'etiquette d'un cote et
        // calculer le renvoi de l'autre serait le pire des cas : le plan et le
        // renvoi se contrediraient.
        painter.setPen(m_style.frame);
        for (int c = 1; c <= folio.frame.columns; ++c) {
            const double x = frame.left() + (c - 0.5) * columnWidth;
            const int numero = folio.frame.columnsRightToLeft ? folio.frame.columns - c + 1 : c;
            drawTextMm(painter, QPointF(x, frame.top() - 1.6), QString::number(numero), 2.2,
                       Primitive::Align::Center);
            drawTextMm(painter, QPointF(x, frame.bottom() + 3.6), QString::number(numero), 2.2,
                       Primitive::Align::Center);
        }
        for (int r = 1; r <= folio.frame.rows; ++r) {
            const double y = frame.top() + (r - 0.5) * rowHeight + 1.1;
            const int rang = folio.frame.rowsBottomToTop ? folio.frame.rows - r + 1 : r;
            const QString letter(QChar(char16_t(u'A' + rang - 1)));
            drawTextMm(painter, QPointF(frame.left() - 3.0, y), letter, 2.2,
                       Primitive::Align::Center);
            drawTextMm(painter, QPointF(frame.right() + 3.0, y), letter, 2.2,
                       Primitive::Align::Center);
        }
    }
    paintBands(painter, folio);
    painter.restore();
}

// LES BANDES DE LOCALISATION.
//
// La feuille coupee sur toute sa hauteur en bandes verticales nommees —
// « CHAMP » | « CABINET 037BJ0151 ». C'est la structure d'un schema de boucle,
// ce que l'echelle de commande est a un schema de commande.
//
// Le trait de separation descend sur TOUTE la hauteur du cadre : c'est ce qui
// dit qu'on change de lieu, pas qu'on change de paragraphe. Un trait qui
// s'arreterait au bandeau se lirait comme une decoration.
void FolioPainter::paintBands(QPainter &painter, const Folio &folio) const
{
    if (folio.bands.isEmpty())
        return;
    const QRectF frame = folio.frameRect();
    const double entete = std::max(3.0, folio.bandHeaderHeight);

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pen(m_style.frame, m_style.frameWidth));

    // Le filet sous le bandeau, sur toute la largeur.
    painter.drawLine(QPointF(frame.left(), frame.top() + entete),
                     QPointF(frame.right(), frame.top() + entete));

    for (int i = 0; i < folio.bands.size(); ++i) {
        const QRectF r = folio.bandRect(i);
        if (i > 0)
            painter.drawLine(QPointF(r.left(), frame.top()), QPointF(r.left(), frame.bottom()));
        painter.setPen(m_style.text);
        drawTextMm(painter, QPointF(r.center().x(), frame.top() + entete * 0.72),
                   folio.bands.at(i).title, entete * 0.5, Primitive::Align::Center);
        painter.setPen(pen(m_style.frame, m_style.frameWidth));
    }
    painter.restore();
}

// LE CARTOUCHE EST LU, PLUS DESSINE EN DUR.
//
// Il portait trois bandes et six textes ecrits ici meme : aucune prise pour
// qui veut le sien. Il se lit maintenant dans un gabarit range dans le projet
// (voir core/titleblock.h). Le peintre ne connait plus AUCUN champ par son
// nom — il pose des cases, et demande leur valeur a `TitleBlock::values`.
// C'est ce qui rend le cartouche modifiable sans toucher une ligne de code.
void FolioPainter::paintTitleBlock(QPainter &painter, const Folio &folio) const
{
    if (!m_style.showTitleBlock)
        return;

    const QRectF block = folio.titleBlockRect();
    if (block.width() < 40.0 || block.height() < 16.0)
        return;

    TitleBlockTemplate gabarit = m_project.titleBlock;
    if (gabarit.isEmpty())
        gabarit = TitleBlock::standard();
    if (gabarit.width <= 0.0 || gabarit.height <= 0.0)
        return;

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pen(m_style.frame, m_style.frameWidth));
    painter.drawRect(block);

    // Le gabarit se dessine a SA taille. Le facteur vaut 1 en usage normal —
    // choisir un gabarit repose sa taille sur les folios. Il n'existe que
    // comme garde-fou : un ancien fichier dont le cadre ne reserve pas la
    // meme place doit serrer son cartouche, jamais deborder sur le dessin.
    // Il est UNIFORME : un cartouche aplati n'est plus lisible.
    const double facteur = std::min(block.width() / gabarit.width,
                                    block.height() / gabarit.height);
    painter.translate(block.topLeft());
    painter.scale(facteur, facteur);

    const QMap<QString, QString> valeurs = TitleBlock::values(m_project, folio);
    for (const TitleBlockCell &cell : gabarit.cells)
        paintTitleBlockCell(painter, cell, valeurs, folio);

    painter.restore();
}

void FolioPainter::paintTitleBlockCell(QPainter &painter, const TitleBlockCell &cell,
                                       const QMap<QString, QString> &values,
                                       const Folio &folio) const
{
    const QRectF r = cell.rect;
    if (r.width() <= 0.0 || r.height() <= 0.0)
        return;

    const QPen filet = pen(m_style.frame, m_style.frameWidth * 0.5);
    if (cell.border) {
        painter.setPen(filet);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);
    }

    const double pad = 1.2;

    switch (cell.kind) {
    case TitleBlockCell::Kind::Image: {
        // Le logo et le sceau viennent du PROJET, jamais d'un chemin sur le
        // disque : une image pointee disparait des que le fichier change de
        // poste, et personne ne s'en apercoit avant l'impression.
        const QByteArray octets = m_project.images.value(cell.key);
        if (octets.isEmpty())
            return;
        QImage img;
        if (!img.loadFromData(octets))
            return;
        // A proportions gardees et centree : un logo etire est pire que pas
        // de logo — c'est l'image de marque d'un bureau d'etudes.
        const QSizeF cible = QSizeF(img.size()).scaled(r.size(), Qt::KeepAspectRatio);
        const QRectF ou(r.center().x() - cible.width() / 2.0,
                        r.center().y() - cible.height() / 2.0, cible.width(), cible.height());
        painter.drawImage(ou, img);
        return;
    }

    case TitleBlockCell::Kind::Text: {
        painter.setPen(m_style.text);
        const double x = cell.align == Primitive::Align::Center ? r.center().x()
                : cell.align == Primitive::Align::Right         ? r.right() - pad
                                                                : r.left() + pad;
        drawTextMm(painter, QPointF(x, r.center().y() + cell.textHeight * 0.4), cell.text,
                   cell.textHeight, cell.align);
        return;
    }

    case TitleBlockCell::Kind::Table: {
        paintTitleBlockTable(painter, cell, folio);
        return;
    }

    case TitleBlockCell::Kind::Field:
        break;
    }

    // Une clef inconnue ecrit du VIDE, jamais son nom : un cartouche qui
    // affiche « projectTitle » en toutes lettres part a l'impression sans que
    // personne ne le remarque.
    const QString valeur = values.value(cell.key);

    if (cell.layout == TitleBlockCell::Layout::Stacked) {
        if (!cell.label.isEmpty()) {
            painter.setPen(m_style.text);
            drawTextMm(painter, QPointF(r.left() + pad, r.top() + cell.labelHeight + pad * 0.6),
                       cell.label.toUpper(), cell.labelHeight);
        }
        const double baseline = r.bottom() - pad;
        const double x = cell.align == Primitive::Align::Center ? r.center().x()
                : cell.align == Primitive::Align::Right         ? r.right() - pad
                                                                : r.left() + pad;
        painter.setPen(m_style.text);
        drawTextMm(painter, QPointF(x, baseline), valeur, cell.textHeight, cell.align);
        return;
    }

    // En ligne : le libelle, puis la valeur a sa suite. Le libelle est mesure
    // pour de vrai — une largeur estimee decalerait la valeur d'un champ a
    // l'autre, et une colonne de valeurs qui ondule se voit tout de suite.
    double x = r.left() + pad;
    const double baseline = r.center().y() + cell.textHeight * 0.4;
    painter.setPen(m_style.text);
    if (!cell.label.isEmpty()) {
        const QString libelle = cell.label.toUpper() + QLatin1Char(':');
        drawTextMm(painter, QPointF(x, baseline), libelle, cell.labelHeight);
        x += textWidthMm(painter.font(), libelle, cell.labelHeight) + pad;
    }
    if (cell.align == Primitive::Align::Center)
        x = r.center().x();
    else if (cell.align == Primitive::Align::Right)
        x = r.right() - pad;
    drawTextMm(painter, QPointF(x, baseline), valeur, cell.textHeight, cell.align);
}

// Une table du cartouche : les revisions, les references, le cheminement.
//
// LES LIGNES GRANDISSENT VERS LE HAUT. L'intitule des colonnes est en bas, la
// premiere ligne juste au-dessus, la suivante encore au-dessus. C'est l'ordre
// dans lequel on relit l'historique d'une planche — la derniere revision
// tombe sous l'oeil en premier — et c'est ce que font les planches relevees.
void FolioPainter::paintTitleBlockTable(QPainter &painter, const TitleBlockCell &cell,
                                        const Folio &folio) const
{
    const QRectF r = cell.rect;
    if (cell.columns.isEmpty())
        return;

    const double rowHeight = std::max(2.6, cell.textHeight * 1.6);
    const int rowsThatFit = int(r.height() / rowHeight) - 1; // moins l'intitule
    if (rowsThatFit < 0)
        return;

    // Largeurs : celles du gabarit si elles sont donnees, sinon a parts
    // egales. Elles sont RELATIVES, donc une table qui change de largeur
    // garde ses proportions.
    QVector<double> poids = cell.widths;
    while (poids.size() < cell.columns.size())
        poids.append(1.0);
    double total = 0.0;
    for (int i = 0; i < cell.columns.size(); ++i)
        total += poids.at(i);
    if (total <= 0.0)
        return;

    const QPen filet = pen(m_style.frame, m_style.frameWidth * 0.5);
    painter.setPen(filet);
    painter.setBrush(Qt::NoBrush);

    const double headerTop = r.bottom() - rowHeight;
    QVector<double> bornes;
    double x = r.left();
    bornes.append(x);
    for (int i = 0; i < cell.columns.size(); ++i) {
        x += r.width() * poids.at(i) / total;
        bornes.append(x);
    }

    const QVector<QStringList> lignes = folio.tables.value(cell.key);
    const int montrees = std::min(int(lignes.size()), rowsThatFit);

    // Le trait du haut de l'intitule, et un trait par ligne reellement ecrite.
    painter.drawLine(QPointF(r.left(), headerTop), QPointF(r.right(), headerTop));
    for (int i = 0; i < montrees; ++i) {
        const double y = headerTop - (i + 1) * rowHeight;
        painter.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    // Les separateurs de colonnes descendent jusqu'en bas, sur toute la table.
    const double top = headerTop - montrees * rowHeight;
    for (int i = 0; i < bornes.size(); ++i)
        painter.drawLine(QPointF(bornes.at(i), top), QPointF(bornes.at(i), r.bottom()));

    // UN TEXTE NE DEBORDE PAS DE SA COLONNE. Une description un peu longue
    // s'ecrivait par-dessus la colonne voisine, et c'est a l'impression qu'on
    // le decouvrait : deux textes superposes dans un cartouche ne se lisent ni
    // l'un ni l'autre, et c'est le cartouche qu'on regarde en premier. On
    // coupe donc, avec des points de suspension — la coupe SE VOIT, ce qui
    // laisse au dessinateur le soin de raccourcir lui-meme.
    const double marge = std::max(0.4, cell.textHeight * 0.25);
    auto tenirDans = [&](QString texte, double largeur) {
        if (largeur <= 0.0 || texte.isEmpty())
            return QString();
        if (textWidthMm(painter.font(), texte, cell.textHeight) <= largeur)
            return texte;
        const QString points = QStringLiteral("…");
        while (!texte.isEmpty()
               && textWidthMm(painter.font(), texte + points, cell.textHeight) > largeur) {
            texte.chop(1);
        }
        return texte.isEmpty() ? QString() : texte + points;
    };

    auto ecrire = [&](const QStringList &cellules, double rowTop) {
        for (int i = 0; i < cell.columns.size() && i < cellules.size(); ++i) {
            const double largeur = bornes.at(i + 1) - bornes.at(i) - 2.0 * marge;
            const QString texte = tenirDans(cellules.at(i), largeur);
            if (texte.isEmpty())
                continue;
            const double cx = (bornes.at(i) + bornes.at(i + 1)) / 2.0;
            drawTextMm(painter, QPointF(cx, rowTop + rowHeight * 0.72), texte, cell.textHeight,
                       Primitive::Align::Center);
        }
    };

    painter.setPen(m_style.text);
    ecrire(cell.columns, headerTop);
    for (int i = 0; i < montrees; ++i)
        ecrire(lignes.at(i), headerTop - (i + 1) * rowHeight);
}

void FolioPainter::paintWire(QPainter &painter, const Wire &wire) const
{
    if (wire.points.size() < 2)
        return;

    // Le type gouverne la couleur, l'epaisseur et le style du trait ; la
    // selection et la mise en evidence passent devant, sinon on ne verrait
    // plus ce qu'on vient de designer.
    const WireType &type = m_project.wireTypes.resolve(wire.wireType);
    const bool typed = m_style.useWireTypeColors && !wire.wireType.isEmpty();
    QColor base = typed ? wireTypeColor(type) : m_style.wire;
    if (typed && m_style.lightenDarkWires && base.lightnessF() < 0.45) {
        // On remonte la clarte sans toucher a la teinte : un L2 noir reste
        // reconnaissable comme gris neutre, un brun reste brun.
        base = QColor::fromHslF(base.hslHueF() < 0.0 ? 0.0 : base.hslHueF(), base.hslSaturationF(),
                                0.62);
    }
    const QColor color = colorFor(wire, base);
    const double width = typed && type.width > 0.0 ? type.width : m_style.wireWidth;
    const Qt::PenStyle dash = wireTypePenStyle(type);
    const int conductors = wire.conductorCount();

    auto wirePen = [&](double w) {
        QPen p = pen(color, w);
        p.setStyle(dash);
        return p;
    };

    painter.save();
    painter.setBrush(Qt::NoBrush);

    if (conductors <= 1) {
        painter.setPen(wirePen(width));
        painter.drawPolyline(wire.points.constData(), int(wire.points.size()));
    } else {
        // Representation unifilaire : un trait unique, barre d'autant de
        // marques obliques qu'il porte de conducteurs.
        painter.setPen(wirePen(width * 1.4));
        painter.drawPolyline(wire.points.constData(), int(wire.points.size()));

        const QPointF a = wire.points.at(0);
        const QPointF b = wire.points.at(1);
        QPointF direction = b - a;
        const double length = std::hypot(direction.x(), direction.y());
        if (length > 6.0) {
            direction /= length;
            const QPointF normal(-direction.y(), direction.x());
            const QPointF anchor = a + direction * (length * 0.5);
            painter.setPen(pen(color, width * 0.8));
            for (int i = 0; i < conductors; ++i) {
                const QPointF centre = anchor + direction * (i - (conductors - 1) / 2.0) * 1.6;
                painter.drawLine(centre - direction * 0.9 - normal * 1.6,
                                 centre + direction * 0.9 + normal * 1.6);
            }
        }
    }

    // LE CODE COULEUR SE LIT SUR LA PLANCHE, pas seulement dans le rapport.
    // C'est l'invariant « ce que le rapport imprime, le dessin le montre » :
    // un cableur qui lit « (N) » sur sa liste doit le retrouver a cote du
    // fil, sinon les deux documents se contredisent. Il ne s'ecrit que quand
    // le type en porte un — un schema de commande n'en a pas, et rien ne
    // change pour lui.
    const QString label = [&] {
        const QString tag = typed ? type.colorTag() : QString();
        if (wire.number.isEmpty())
            return tag;
        return tag.isEmpty() ? wire.number : wire.number + QLatin1Char(' ') + tag;
    }();

    if (m_style.showWireNumbers && !label.isEmpty()) {
        const QPointF a = wire.points.at(0);
        const QPointF b = wire.points.at(1);
        const bool vertical = std::abs(b.y() - a.y()) > std::abs(b.x() - a.x());
        const QPointF middle = (a + b) / 2.0;
        painter.setPen(m_style.tag);
        // Le repere se pose a cote du fil, jamais dessus : il doit rester
        // lisible sur un folio dense.
        const QPointF at = vertical ? middle + QPointF(1.2, -0.8) : middle + QPointF(0.0, -1.2);
        drawTextMm(painter, at, label, m_style.wireNumberHeight,
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

    // Le stylo est en millimetres et la transformation qu'on vient de poser
    // porte le facteur d'echelle du symbole : sans compensation, un symbole
    // deux fois plus grand serait aussi trace deux fois plus epais. Grossir
    // change les dimensions, pas la plume.
    const double placed = std::abs(symbol.placement.scale);
    const double inkScale = placed > 1e-9 ? 1.0 / placed : 1.0;

    if (!definition) {
        // Symbole introuvable : on trace un cadre barre plutot que rien. Un
        // trou invisible dans un schema est bien pire qu'une marque explicite.
        painter.setPen(pen(m_style.highlight, m_style.symbolWidth * inkScale));
        painter.setBrush(Qt::NoBrush);
        const QRectF box(-5, -5, 10, 10);
        painter.drawRect(box);
        painter.drawLine(box.topLeft(), box.bottomRight());
        painter.drawLine(box.bottomLeft(), box.topRight());
        painter.restore();

        // Le cadre barre dit qu'il manque un symbole ; il ne disait pas
        // lequel. Un dessinateur qui tombe dessus a besoin du nom pour savoir
        // quelle bibliotheque rouvrir — sans lui, la marque est une enigme.
        painter.save();
        painter.setPen(m_style.highlight);
        drawTextMm(painter, symbol.placement.position + QPointF(0.0, 8.0),
                   symbol.definitionId, 2.0, Primitive::Align::Center);
        painter.restore();
        return;
    }

    painter.setBrush(Qt::NoBrush);
    for (const Primitive &primitive : definition->graphics) {
        const double width = primitive.lineWidth > 0.0 ? primitive.lineWidth
                                                       : m_style.symbolWidth;
        QPen trait = pen(color, width * inkScale);
        applyStroke(trait, primitive.stroke);
        painter.setPen(trait);
        paintPrimitive(painter, primitive);
    }

    painter.setPen(pen(color, m_style.symbolWidth * inkScale));
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
    // LE NUMERO DE BORNE. Il etait gere par l'editeur de borniers, imprime
    // par le rapport de cablage... et invisible sur le dessin. Le plan et le
    // rapport se contredisaient donc : un cableur lisait « X1:4 » sur sa
    // feuille et ne trouvait sur le schema qu'une borne anonyme. Il se lit du
    // cote de la valeur, parce qu'une borne n'a pas de calibre a montrer, et
    // il suit le meme interrupteur que le repere : c'est l'identite de la
    // borne, pas une annotation.
    if (m_style.showDesignations && definition->deviceKind == QLatin1String("terminal")) {
        const QString numero = symbol.fields.value(QStringLiteral("terminal"));
        if (!numero.isEmpty()) {
            painter.setPen(m_selection.contains(symbol.id()) ? m_style.selection : m_style.tag);
            drawTextMm(painter, symbol.placement.map(definition->valueAnchor), numero,
                       m_style.designationHeight, Primitive::Align::Center);
        }
    }
    if (m_style.showValues) {
        const QString value = symbol.fields.value(QStringLiteral("value"));
        // Une borne numerotee occupe deja l'ancre de valeur : on n'y ecrit pas
        // deux textes l'un sur l'autre.
        const bool priseParLaBorne =
                definition->deviceKind == QLatin1String("terminal")
                && !symbol.fields.value(QStringLiteral("terminal")).isEmpty();
        if (!value.isEmpty() && !priseParLaBorne) {
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
    // Une source est pleine, une destination creuse. Le sens du signal se lit
    // alors sans lire le texte, ce qui est tout l'interet d'une fleche.
    if (label.role == Label::Role::Source) {
        QColor fill = color;
        fill.setAlpha(48);
        painter.setBrush(fill);
    }
    painter.drawPolygon(outline);
    painter.setBrush(Qt::NoBrush);

    painter.setPen(color);
    drawTextMm(painter, (tip + tail) / 2.0 + QPointF(0.0, label.height * 0.35), label.name,
               label.height, Primitive::Align::Center);

    // Le renvoi, sous la fleche : « → 2/A3 ». Sans lui, retrouver l'autre bout
    // demande de feuilleter tout le dossier.
    const auto reference = m_crossRefs.constFind(label.id());
    if (reference != m_crossRefs.constEnd() && !reference.value().isEmpty()) {
        const double small = label.height * 0.78;
        // Le renvoi se pose au-dela de la queue, dans le sens de la fleche :
        // c'est le seul cote ou il ne recouvre ni la fleche ni le fil, quelle
        // que soit l'orientation.
        QPointF at = tail + direction * (small * 1.4);
        if (std::abs(direction.y()) < 0.5)
            at += QPointF(0.0, small * 0.4); // texte horizontal : centrage vertical
        painter.setPen(m_style.tag);
        drawTextMm(painter, at, reference.value(), small, Primitive::Align::Center);
    }
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
        QPen trait = pen(colorFor(entity, m_style.symbol),
                         item.shape.lineWidth > 0.0 ? item.shape.lineWidth : m_style.symbolWidth);
        applyStroke(trait, item.shape.stroke);
        painter.setPen(trait);
        painter.setBrush(Qt::NoBrush);
        paintPrimitive(painter, item.shape);
        painter.restore();
        break;
    }
    case EntityType::Label:
        paintLabel(painter, static_cast<const Label &>(entity));
        break;
    case EntityType::Dimension:
        paintDimension(painter, static_cast<const DimensionItem &>(entity));
        break;
    }
}

// La cote se dessine en UN endroit, comme tout le reste : c'est ce qui fait
// que l'ecran, le PDF et l'apercu montrent le meme trait. La geometrie, elle,
// vient du coeur (DimensionItem::geometry) — le peintre ne la recalcule pas,
// sinon l'export DXF et lui divergeraient d'un demi-millimetre, et cela se
// voit sur une fleche.
void FolioPainter::paintDimension(QPainter &painter, const DimensionItem &item) const
{
    const auto g = item.geometry();
    painter.save();
    QPen trait = pen(colorFor(item, m_style.dimension), m_style.dimensionWidth);
    painter.setPen(trait);
    painter.setBrush(Qt::NoBrush);

    // Lignes d'attache et ligne de cote.
    painter.drawLine(g.firstFrom, g.firstTo);
    painter.drawLine(g.secondFrom, g.secondTo);
    painter.drawLine(g.lineStart, g.lineEnd);

    // Les fleches. Elles sont pleines et tournees vers l'exterieur quand la
    // cote est trop courte pour les contenir — sinon deux fleches qui se
    // croisent au milieu d'une cote de 3 mm ne se lisent plus.
    QPointF direction = g.lineEnd - g.lineStart;
    const double longueur = std::hypot(direction.x(), direction.y());
    if (longueur > kEpsilon) {
        direction /= longueur;
        const double taille = m_style.dimensionArrow;
        const bool dehors = longueur < taille * 2.5;
        const QPointF normale(-direction.y(), direction.x());
        auto fleche = [&](const QPointF &pointe, const QPointF &vers) {
            const QPointF base = pointe - vers * taille;
            QPolygonF tete;
            tete << pointe << base + normale * (taille * 0.16)
                 << base - normale * (taille * 0.16);
            painter.setBrush(trait.color());
            painter.drawPolygon(tete);
            painter.setBrush(Qt::NoBrush);
        };
        fleche(g.lineStart, dehors ? -direction : direction);
        fleche(g.lineEnd, dehors ? direction : -direction);
        if (dehors) {
            // La ligne de cote se prolonge pour porter les fleches posees a
            // l'exterieur, sinon elles flottent.
            painter.drawLine(g.lineStart, g.lineStart - direction * taille * 1.5);
            painter.drawLine(g.lineEnd, g.lineEnd + direction * taille * 1.5);
        }
    }

    // Le texte, POSE AU-DESSUS DE LA LIGNE DE COTE, au sens du texte lui-meme :
    // la convention de dessin le veut du meme cote quel que soit l'angle. Le
    // vecteur « haut » du texte incline de a vaut (sin a, -cos a) — l'angle
    // etant deja ramene dans (-90, 90], la cote ne se lit jamais la tete en
    // bas. Il est en millimetres comme tout le reste : sa taille ne suit pas
    // le zoom.
    const double radians = g.angleDegrees * 3.14159265358979323846 / 180.0;
    const QPointF haut(std::sin(radians), -std::cos(radians));
    const QPointF pose = g.textAt + haut * (item.textHeight * 0.5);
    drawTextMm(painter, pose, item.displayText(), item.textHeight, Primitive::Align::Center,
               g.angleDegrees);
    painter.restore();
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
