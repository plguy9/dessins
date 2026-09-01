#include "reportplacer.h"

#include "core/entities.h"

#include <algorithm>

namespace dsn {

namespace {

// Largeur estimee d'un texte. Le coeur ne charge pas de police : c'est le
// meme facteur que TextItem::boundingBox, pour que la table et le reste du
// dessin se trompent au moins de la meme facon.
double textWidth(const QString &text, double height)
{
    return height * 0.62 * std::max(1, int(text.size()));
}

void addLine(std::vector<EntityPtr> &out, const QPointF &a, const QPointF &b, double width)
{
    auto item = std::make_unique<GraphicItem>();
    item->shape = Primitive::line(a, b, width);
    out.push_back(std::move(item));
}

void addText(std::vector<EntityPtr> &out, const QPointF &at, const QString &text, double height)
{
    if (text.isEmpty())
        return;
    auto item = std::make_unique<TextItem>();
    item->text = text;
    item->placement.position = at;
    item->height = height;
    item->align = TextItem::Align::Left;
    out.push_back(std::move(item));
}

// Decoupe les lignes en sections posees cote a cote.
QVector<QPair<int, int>> sectionsOf(const ReportTable &table, const ReportTableSpec &spec)
{
    QVector<QPair<int, int>> sections;
    const int total = table.rowCount();
    const int perSection = spec.rowsPerSection > 0 ? spec.rowsPerSection : total;
    for (int start = 0; start < total; start += perSection)
        sections.append({ start, std::min(perSection, total - start) });
    if (sections.isEmpty())
        sections.append({ 0, 0 });
    return sections;
}

} // namespace

QVector<double> ReportPlacer::columnWidths(const ReportTable &table, const ReportTableSpec &spec)
{
    const int columns = int(table.headers.size());
    if (spec.explicitWidths.size() == columns)
        return spec.explicitWidths;

    QVector<double> widths(columns, 0.0);
    for (int c = 0; c < columns; ++c)
        widths[c] = textWidth(table.headers.at(c), spec.textHeight);
    for (const QStringList &row : table.rows) {
        for (int c = 0; c < columns && c < row.size(); ++c)
            widths[c] = std::max(widths[c], textWidth(row.at(c), spec.textHeight));
    }
    for (double &w : widths)
        w += spec.padding * 2.0;
    return widths;
}

QRectF ReportPlacer::bounds(const ReportTable &table, const ReportTableSpec &spec)
{
    if (table.headers.isEmpty())
        return {};
    const QVector<double> widths = columnWidths(table, spec);
    double sectionWidth = 0.0;
    for (double w : widths)
        sectionWidth += w;

    const auto sections = sectionsOf(table, spec);
    int tallest = 0;
    for (const auto &section : sections)
        tallest = std::max(tallest, section.second);

    const double titleHeight = spec.withTitle && !table.title.isEmpty()
            ? spec.rowHeight * 1.4
            : 0.0;
    const double height = titleHeight
            + (tallest + (spec.withHeaders ? 1 : 0)) * spec.rowHeight;
    const double width = sections.size() * sectionWidth
            + (sections.size() - 1) * spec.sectionGap;
    return QRectF(spec.origin, QSizeF(width, height));
}

std::vector<EntityPtr> ReportPlacer::build(const ReportTable &table, const ReportTableSpec &spec)
{
    std::vector<EntityPtr> out;
    if (table.headers.isEmpty() || table.rows.isEmpty())
        return out;

    const QVector<double> widths = columnWidths(table, spec);
    double sectionWidth = 0.0;
    for (double w : widths)
        sectionWidth += w;

    const auto sections = sectionsOf(table, spec);
    const double titleHeight = spec.withTitle && !table.title.isEmpty()
            ? spec.rowHeight * 1.4
            : 0.0;

    if (titleHeight > 0.0) {
        addText(out, spec.origin + QPointF(0.0, spec.textHeight * 1.2), table.title,
                spec.textHeight * 1.35);
    }

    for (int s = 0; s < sections.size(); ++s) {
        const int firstRow = sections.at(s).first;
        const int rowCount = sections.at(s).second;
        if (rowCount <= 0)
            continue;

        const double left = spec.origin.x() + s * (sectionWidth + spec.sectionGap);
        const double top = spec.origin.y() + titleHeight;
        const int lineCount = rowCount + (spec.withHeaders ? 1 : 0);
        const double bottom = top + lineCount * spec.rowHeight;

        // Traits horizontaux, y compris le haut et le bas.
        for (int i = 0; i <= lineCount; ++i) {
            const double y = top + i * spec.rowHeight;
            // Le trait sous les intitules est plus epais : c'est ce qui
            // distingue l'en-tete du corps une fois la table imprimee.
            const double width = (i == 0 || i == lineCount
                                  || (spec.withHeaders && i == 1))
                    ? spec.lineWidth * 2.0
                    : spec.lineWidth;
            addLine(out, QPointF(left, y), QPointF(left + sectionWidth, y), width);
        }

        // Traits verticaux.
        double x = left;
        addLine(out, QPointF(x, top), QPointF(x, bottom), spec.lineWidth * 2.0);
        for (int c = 0; c < widths.size(); ++c) {
            x += widths.at(c);
            const bool last = c == widths.size() - 1;
            addLine(out, QPointF(x, top), QPointF(x, bottom),
                    last ? spec.lineWidth * 2.0 : spec.lineWidth);
        }

        // Le texte se pose sur la ligne de base, a un tiers de la hauteur de
        // rang au-dessus du trait du bas : c'est ce qui le centre a l'oeil.
        const double baseline = spec.rowHeight - (spec.rowHeight - spec.textHeight) / 2.0;

        int line = 0;
        if (spec.withHeaders) {
            double cell = left;
            for (int c = 0; c < widths.size(); ++c) {
                addText(out, QPointF(cell + spec.padding, top + baseline),
                        table.headers.at(c), spec.textHeight);
                cell += widths.at(c);
            }
            ++line;
        }

        for (int r = 0; r < rowCount; ++r, ++line) {
            const QStringList &row = table.rows.at(firstRow + r);
            double cell = left;
            for (int c = 0; c < widths.size(); ++c) {
                if (c < row.size()) {
                    addText(out, QPointF(cell + spec.padding, top + line * spec.rowHeight
                                                         + baseline),
                            row.at(c), spec.textHeight);
                }
                cell += widths.at(c);
            }
        }
    }

    return out;
}

} // namespace dsn
