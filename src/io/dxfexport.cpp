#include "dxfexport.h"

#include "core/entities.h"
#include "core/wiretype.h"

#include <QPair>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <iterator>

namespace dsn {

namespace {

// Calques. Separer les fils des symboles et les reperes du reste est ce qui
// rend le fichier reellement exploitable une fois ouvert ailleurs.
constexpr auto kLayerSymbols = "SYMBOLES";
constexpr auto kLayerWires = "FILS";
constexpr auto kLayerTags = "REPERES";
constexpr auto kLayerText = "TEXTES";
constexpr auto kLayerFrame = "CADRE";

struct LayerDef {
    const char *name;
    int color;
};

const LayerDef kLayers[] = {
    { kLayerSymbols, 7 }, // blanc / noir
    { kLayerWires, 5 },   // bleu
    { kLayerTags, 3 },    // vert
    { kLayerText, 4 },    // cyan
    { kLayerFrame, 8 },   // gris
};

// Un calque DXF R12 ne porte pas de couleur vraie : seulement un indice ACI.
// On projette donc la couleur du type de fil sur l'indice le plus proche.
// La table ne reprend pas les 255 indices — ceux-ci suffisent a placer une
// couleur de conducteur (brun, bleu, vert-jaune, rouge, gris) sur une teinte
// qu'un dessinateur reconnaitra en ouvrant le fichier ailleurs.
struct AciColor {
    int index;
    int r, g, b;
};

const AciColor kAciPalette[] = {
    { 1, 255, 0, 0 },     { 2, 255, 255, 0 },   { 3, 0, 255, 0 },
    { 4, 0, 255, 255 },   { 5, 0, 0, 255 },     { 6, 255, 0, 255 },
    { 7, 255, 255, 255 }, { 8, 128, 128, 128 }, { 9, 192, 192, 192 },
    { 14, 127, 0, 0 },    { 30, 255, 127, 0 },  { 34, 127, 63, 0 },
    { 40, 255, 191, 0 },  { 54, 127, 127, 0 },  { 70, 191, 255, 0 },
    { 84, 63, 127, 0 },   { 94, 0, 127, 0 },    { 130, 0, 255, 191 },
    { 150, 0, 191, 255 }, { 154, 0, 95, 127 },  { 170, 0, 0, 255 },
    { 174, 0, 0, 127 },   { 190, 127, 0, 255 }, { 210, 255, 0, 191 },
    { 250, 51, 51, 51 },  { 252, 132, 132, 132 }, { 254, 214, 214, 214 },
};

int aciFromRgb(quint32 rgb)
{
    const int r = int((rgb >> 16) & 0xFFu);
    const int g = int((rgb >> 8) & 0xFFu);
    const int b = int(rgb & 0xFFu);
    int best = 7;
    long long bestDistance = -1;
    for (const AciColor &c : kAciPalette) {
        const long long d = 1LL * (c.r - r) * (c.r - r) + 1LL * (c.g - g) * (c.g - g)
                            + 1LL * (c.b - b) * (c.b - b);
        if (bestDistance < 0 || d < bestDistance) {
            bestDistance = d;
            best = c.index;
        }
    }
    return best;
}

class DxfWriter
{
public:
    explicit DxfWriter(QByteArray &buffer) : m_stream(&buffer, QIODevice::WriteOnly)
    {
        m_stream.setEncoding(QStringConverter::Utf8);
        m_stream.setRealNumberNotation(QTextStream::FixedNotation);
        m_stream.setRealNumberPrecision(4);
    }

    void code(int group, const QString &value)
    {
        m_stream << group << "\n" << value << "\n";
    }
    void code(int group, const char *value) { code(group, QString::fromLatin1(value)); }
    void code(int group, int value) { m_stream << group << "\n" << value << "\n"; }
    void code(int group, double value) { m_stream << group << "\n" << value << "\n"; }

    void flush() { m_stream.flush(); }

private:
    QTextStream m_stream;
};

// Le DXF a son axe des ordonnees vers le haut, le document vers le bas.
// Le renversement se fait une seule fois, ici, autour de la hauteur de feuille.
struct Flip {
    double height = 0.0;
    QPointF operator()(const QPointF &p) const { return QPointF(p.x(), height - p.y()); }
    // Dans un bloc, il n'y a pas de hauteur de feuille : seule la composante
    // lineaire du renversement s'applique.
    static QPointF local(const QPointF &p) { return QPointF(p.x(), -p.y()); }
};

double normalizeAngle(double degrees)
{
    double a = std::fmod(degrees, 360.0);
    if (a < 0.0)
        a += 360.0;
    return a;
}

// Le nom AutoCAD du type de trait d'une primitive. Un trait plein n'ecrit
// rien : l'entite herite alors du calque, ce qui est la convention.
const char *linetypeName(Primitive::Stroke stroke)
{
    switch (stroke) {
    case Primitive::Stroke::Dashed: return "DASHED";
    case Primitive::Stroke::Dotted: return "DOT";
    case Primitive::Stroke::DashDot: return "DASHDOT";
    case Primitive::Stroke::Solid: break;
    }
    return nullptr;
}

void writeStroke(DxfWriter &w, Primitive::Stroke stroke)
{
    if (const char *name = linetypeName(stroke))
        w.code(6, name);
}

void writeLine(DxfWriter &w, const QString &layer, const QPointF &a, const QPointF &b,
               Primitive::Stroke stroke = Primitive::Stroke::Solid)
{
    w.code(0, "LINE");
    w.code(8, layer);
    writeStroke(w, stroke);
    w.code(10, a.x());
    w.code(20, a.y());
    w.code(30, 0.0);
    w.code(11, b.x());
    w.code(21, b.y());
    w.code(31, 0.0);
}

void writeCircle(DxfWriter &w, const QString &layer, const QPointF &centre, double radius,
                 Primitive::Stroke stroke = Primitive::Stroke::Solid)
{
    w.code(0, "CIRCLE");
    w.code(8, layer);
    writeStroke(w, stroke);
    w.code(10, centre.x());
    w.code(20, centre.y());
    w.code(30, 0.0);
    w.code(40, radius);
}

void writeArc(DxfWriter &w, const QString &layer, const QPointF &centre, double radius,
              double startAngle, double spanAngle,
              Primitive::Stroke stroke = Primitive::Stroke::Solid)
{
    // Le renversement de l'axe des ordonnees conjugue la rotation : les angles
    // passent inchanges, seul le repere change de main.
    w.code(0, "ARC");
    w.code(8, layer);
    writeStroke(w, stroke);
    w.code(10, centre.x());
    w.code(20, centre.y());
    w.code(30, 0.0);
    w.code(40, radius);
    if (spanAngle >= 0.0) {
        w.code(50, normalizeAngle(startAngle));
        w.code(51, normalizeAngle(startAngle + spanAngle));
    } else {
        w.code(50, normalizeAngle(startAngle + spanAngle));
        w.code(51, normalizeAngle(startAngle));
    }
}

// Le R12 ne connait pas LWPOLYLINE : une polyligne s'ecrit en POLYLINE, une
// suite de VERTEX et un SEQEND.
void writePolyline(DxfWriter &w, const QString &layer, const QVector<QPointF> &points, bool closed,
                   Primitive::Stroke stroke = Primitive::Stroke::Solid)
{
    if (points.size() < 2)
        return;
    w.code(0, "POLYLINE");
    w.code(8, layer);
    writeStroke(w, stroke);
    w.code(66, 1); // des sommets suivent
    w.code(10, 0.0);
    w.code(20, 0.0);
    w.code(30, 0.0);
    w.code(70, closed ? 1 : 0);

    for (const QPointF &p : points) {
        w.code(0, "VERTEX");
        w.code(8, layer);
        w.code(10, p.x());
        w.code(20, p.y());
        w.code(30, 0.0);
    }
    w.code(0, "SEQEND");
    w.code(8, layer);
}

void writeText(DxfWriter &w, const QString &layer, const QPointF &at, const QString &text,
               double height, Primitive::Align align, double rotation = 0.0)
{
    if (text.isEmpty())
        return;
    w.code(0, "TEXT");
    w.code(8, layer);
    w.code(10, at.x());
    w.code(20, at.y());
    w.code(30, 0.0);
    w.code(40, height);
    w.code(1, text);
    if (!fuzzyZero(rotation))
        w.code(50, normalizeAngle(rotation));
    const int halign = align == Primitive::Align::Center ? 1
                     : align == Primitive::Align::Right  ? 2
                                                         : 0;
    if (halign != 0) {
        w.code(72, halign);
        // Un texte cadre exige un second point d'alignement, sans quoi le
        // lecteur retombe sur le point d'insertion et le cadrage est perdu.
        w.code(11, at.x());
        w.code(21, at.y());
        w.code(31, 0.0);
    }
}

void writePrimitive(DxfWriter &w, const QString &layer, const Primitive &primitive,
                    const std::function<QPointF(const QPointF &)> &map)
{
    const Primitive::Stroke trait = primitive.stroke;
    switch (primitive.kind) {
    case Primitive::Kind::Line:
        if (primitive.points.size() >= 2)
            writeLine(w, layer, map(primitive.points.at(0)), map(primitive.points.at(1)), trait);
        break;
    case Primitive::Kind::Polyline: {
        QVector<QPointF> mapped;
        mapped.reserve(primitive.points.size());
        for (const QPointF &p : primitive.points)
            mapped.append(map(p));
        writePolyline(w, layer, mapped, false, trait);
        break;
    }
    case Primitive::Kind::Rect: {
        if (primitive.points.size() < 2)
            break;
        const QRectF r = normalized(primitive.points.at(0), primitive.points.at(1));
        const QVector<QPointF> corners{ map(r.topLeft()), map(r.topRight()), map(r.bottomRight()),
                                        map(r.bottomLeft()) };
        writePolyline(w, layer, corners, true, trait);
        break;
    }
    case Primitive::Kind::Circle:
        if (!primitive.points.isEmpty())
            writeCircle(w, layer, map(primitive.points.first()), primitive.radius, trait);
        break;
    case Primitive::Kind::Arc:
        if (!primitive.points.isEmpty())
            writeArc(w, layer, map(primitive.points.first()), primitive.radius,
                     primitive.startAngle, primitive.spanAngle, trait);
        break;
    case Primitive::Kind::Text:
        if (!primitive.points.isEmpty())
            writeText(w, layer, map(primitive.points.first()), primitive.text,
                      primitive.textHeight, primitive.align);
        break;
    }
}

} // namespace

QString DxfExport::sanitizeName(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar c : raw) {
        const char16_t u = c.unicode();
        if ((u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'_' || u == u'-'
            || u == u'$')
            out.append(c);
        else if (u >= u'a' && u <= u'z')
            out.append(c.toUpper());
        else
            out.append(QLatin1Char('_'));
    }
    if (out.isEmpty())
        out = QStringLiteral("BLOC");
    // Un nom de bloc R12 ne peut pas commencer par un chiffre.
    if (out.at(0).isDigit())
        out.prepend(QLatin1Char('B'));
    return out.left(31);
}

QByteArray DxfExport::encodeFolio(const Project &project, const Folio &folio,
                                  const DxfExportOptions &options)
{
    QByteArray buffer;
    DxfWriter w(buffer);
    const Flip flip{ folio.sheet.height };

    // Definitions de symboles reellement utilisees par ce folio : inutile
    // d'exporter toute la bibliotheque dans chaque fichier.
    QHash<QString, QString> blockNames; // definitionId -> nom de bloc
    QSet<QString> usedNames;
    for (const SymbolInstance *symbol : folio.entitiesOfType<SymbolInstance>()) {
        if (blockNames.contains(symbol->definitionId))
            continue;
        if (!project.library.definition(symbol->definitionId))
            continue;
        QString name = sanitizeName(symbol->definitionId);
        int suffix = 1;
        while (usedNames.contains(name))
            name = sanitizeName(symbol->definitionId).left(28) + QString::number(++suffix);
        usedNames.insert(name);
        blockNames.insert(symbol->definitionId, name);
    }

    // ---- HEADER --------------------------------------------------------
    w.code(0, "SECTION");
    w.code(2, "HEADER");
    w.code(9, "$ACADVER");
    w.code(1, "AC1009");
    w.code(9, "$INSUNITS");
    w.code(70, 4); // millimetres
    w.code(9, "$EXTMIN");
    w.code(10, 0.0);
    w.code(20, 0.0);
    w.code(30, 0.0);
    w.code(9, "$EXTMAX");
    w.code(10, folio.sheet.width);
    w.code(20, folio.sheet.height);
    w.code(30, 0.0);
    w.code(0, "ENDSEC");

    // ---- TABLES --------------------------------------------------------
    w.code(0, "SECTION");
    w.code(2, "TABLES");

    // Les types de trait. Un cadre d'armoire se trace en pointille : sans la
    // declaration ici, l'entite le demanderait et AutoCAD la rendrait pleine.
    // Les longueurs sont en unites de dessin, donc en millimetres, et
    // reprennent celles du peintre — l'export doit ressembler a l'ecran.
    w.code(0, "TABLE");
    w.code(2, "LTYPE");
    w.code(70, 4);
    w.code(0, "LTYPE");
    w.code(2, "CONTINUOUS");
    w.code(70, 0);
    w.code(3, "Trait continu");
    w.code(72, 65);
    w.code(73, 0);
    w.code(40, 0.0);

    w.code(0, "LTYPE");
    w.code(2, "DASHED");
    w.code(70, 0);
    w.code(3, "Tirets ___ ___ ___");
    w.code(72, 65);
    w.code(73, 2);
    w.code(40, 5.0);
    w.code(49, 3.0);
    w.code(74, 0);
    w.code(49, -2.0);
    w.code(74, 0);

    w.code(0, "LTYPE");
    w.code(2, "DOT");
    w.code(70, 0);
    w.code(3, "Points . . . . .");
    w.code(72, 65);
    w.code(73, 2);
    w.code(40, 2.0);
    w.code(49, 0.5);
    w.code(74, 0);
    w.code(49, -1.5);
    w.code(74, 0);

    w.code(0, "LTYPE");
    w.code(2, "DASHDOT");
    w.code(70, 0);
    w.code(3, "Mixte ___ . ___ .");
    w.code(72, 65);
    w.code(73, 4);
    w.code(40, 10.5);
    w.code(49, 6.0);
    w.code(74, 0);
    w.code(49, -2.0);
    w.code(74, 0);
    w.code(49, 0.5);
    w.code(74, 0);
    w.code(49, -2.0);
    w.code(74, 0);
    w.code(0, "ENDTAB");

    // Chaque type de fil du projet devient un calque, comme le fait AutoCAD
    // Electrical : le fichier s'ouvre ailleurs avec ses potentiels deja
    // separes, et non en un seul paquet de polylignes.
    QHash<QString, QString> wireLayers; // id de type -> nom de calque
    QList<QPair<QString, int>> extraLayers;
    QSet<QString> layerNames;
    for (const LayerDef &layer : kLayers)
        layerNames.insert(QString::fromLatin1(layer.name));
    for (const WireType &type : project.wireTypes.all()) {
        if (type.layer.isEmpty())
            continue;
        const QString name = sanitizeName(type.layer).left(31);
        wireLayers.insert(type.id, name);
        if (layerNames.contains(name))
            continue;
        layerNames.insert(name);
        extraLayers.append({ name, aciFromRgb(type.rgb) });
    }

    w.code(0, "TABLE");
    w.code(2, "LAYER");
    w.code(70, int(std::size(kLayers)) + int(extraLayers.size()));
    for (const LayerDef &layer : kLayers) {
        w.code(0, "LAYER");
        w.code(2, layer.name);
        w.code(70, 0);
        w.code(62, layer.color);
        w.code(6, "CONTINUOUS");
    }
    for (const auto &layer : extraLayers) {
        w.code(0, "LAYER");
        w.code(2, layer.first);
        w.code(70, 0);
        w.code(62, layer.second);
        w.code(6, "CONTINUOUS");
    }
    w.code(0, "ENDTAB");
    w.code(0, "ENDSEC");

    // ---- BLOCKS --------------------------------------------------------
    w.code(0, "SECTION");
    w.code(2, "BLOCKS");
    for (auto it = blockNames.constBegin(); it != blockNames.constEnd(); ++it) {
        const SymbolDefinition *definition = project.library.definition(it.key());
        if (!definition)
            continue;

        w.code(0, "BLOCK");
        w.code(8, kLayerSymbols);
        w.code(2, it.value());
        w.code(70, 0);
        w.code(10, 0.0);
        w.code(20, 0.0);
        w.code(30, 0.0);
        w.code(3, it.value());
        w.code(1, "");

        for (const Primitive &primitive : definition->graphics)
            writePrimitive(w, QLatin1String(kLayerSymbols), primitive, Flip::local);

        for (const Pin &pin : definition->pins) {
            writeLine(w, QLatin1String(kLayerSymbols), Flip::local(pin.root()),
                      Flip::local(pin.position));
            if (options.includePinNumbers && pin.showNumber && !pin.number.isEmpty()) {
                writeText(w, QLatin1String(kLayerTags),
                          Flip::local(pin.root() + QPointF(0.8, -0.8)), pin.number, 1.5,
                          Primitive::Align::Left);
            }
        }

        w.code(0, "ENDBLK");
        w.code(8, kLayerSymbols);
    }
    w.code(0, "ENDSEC");

    // ---- ENTITIES ------------------------------------------------------
    w.code(0, "SECTION");
    w.code(2, "ENTITIES");

    if (options.includeFrame) {
        const QRectF frame = folio.frameRect();
        writePolyline(w, QLatin1String(kLayerFrame),
                      { flip(frame.topLeft()), flip(frame.topRight()), flip(frame.bottomRight()),
                        flip(frame.bottomLeft()) },
                      true);
        if (options.includeTitleBlock) {
            const QRectF block = folio.titleBlockRect();
            writePolyline(w, QLatin1String(kLayerFrame),
                          { flip(block.topLeft()), flip(block.topRight()),
                            flip(block.bottomRight()), flip(block.bottomLeft()) },
                          true);
            const QString caption = project.info.title.isEmpty() ? folio.title
                                                                 : project.info.title;
            writeText(w, QLatin1String(kLayerFrame),
                      flip(block.topLeft() + QPointF(4.0, 8.0)), caption, 3.5,
                      Primitive::Align::Left);
            writeText(w, QLatin1String(kLayerFrame),
                      flip(block.topLeft() + QPointF(4.0, 16.0)),
                      QStringLiteral("Folio ") + folio.number, 2.5, Primitive::Align::Left);
        }
    }

    for (const EntityPtr &entity : folio.entities()) {
        switch (entity->type()) {
        case EntityType::Wire: {
            const auto *wire = static_cast<const Wire *>(entity.get());
            QVector<QPointF> mapped;
            mapped.reserve(wire->points.size());
            for (const QPointF &p : wire->points)
                mapped.append(flip(p));
            // Un type inconnu ou sans calque retombe sur FILS : un fil ne
            // doit jamais disparaitre de l'export.
            const QString layer = wireLayers.value(wire->wireType, QLatin1String(kLayerWires));
            writePolyline(w, layer, mapped, false);

            if (options.includeWireNumbers && !wire->number.isEmpty() && wire->points.size() >= 2) {
                const QPointF a = wire->points.at(0);
                const QPointF b = wire->points.at(1);
                const QPointF mid = flip((a + b) / 2.0) + QPointF(0.0, 1.2);
                writeText(w, QLatin1String(kLayerTags), mid, wire->number, 2.0,
                          Primitive::Align::Center);
            }
            break;
        }
        case EntityType::Junction: {
            const auto *junction = static_cast<const Junction *>(entity.get());
            writeCircle(w, QLatin1String(kLayerWires), flip(junction->point),
                        junction->diameter / 2.0);
            break;
        }
        case EntityType::Symbol: {
            const auto *symbol = static_cast<const SymbolInstance *>(entity.get());
            const auto name = blockNames.constFind(symbol->definitionId);
            if (name == blockNames.constEnd())
                break;

            const QPointF insertion = flip(symbol->placement.position);
            w.code(0, "INSERT");
            w.code(8, kLayerSymbols);
            w.code(2, name.value());
            w.code(10, insertion.x());
            w.code(20, insertion.y());
            w.code(30, 0.0);
            w.code(41, symbol->placement.mirrored ? -1.0 : 1.0);
            w.code(42, 1.0);
            w.code(43, 1.0);
            // Le renversement de l'axe des ordonnees inverse le sens des
            // rotations : l'angle du document se lit a l'envers en DXF.
            w.code(50, normalizeAngle(-double(toDegrees(symbol->placement.orientation))));

            if (options.includeDesignations) {
                const SymbolDefinition *definition =
                        project.library.definition(symbol->definitionId);
                const QString designation = symbol->designation();
                if (definition && !designation.isEmpty()) {
                    const QPointF at =
                            flip(symbol->placement.map(definition->designationAnchor));
                    writeText(w, QLatin1String(kLayerTags), at, designation, 2.5,
                              Primitive::Align::Center);
                }
                const QString value = symbol->fields.value(QStringLiteral("value"));
                if (definition && !value.isEmpty()) {
                    const QPointF at = flip(symbol->placement.map(definition->valueAnchor));
                    writeText(w, QLatin1String(kLayerTags), at, value, 2.0,
                              Primitive::Align::Center);
                }
            }
            break;
        }
        case EntityType::Text: {
            const auto *item = static_cast<const TextItem *>(entity.get());
            const auto align = item->align == TextItem::Align::Center ? Primitive::Align::Center
                             : item->align == TextItem::Align::Right  ? Primitive::Align::Right
                                                                      : Primitive::Align::Left;
            writeText(w, QLatin1String(kLayerText), flip(item->placement.position), item->text,
                      item->height, align,
                      -double(toDegrees(item->placement.orientation)));
            break;
        }
        case EntityType::Graphic: {
            const auto *item = static_cast<const GraphicItem *>(entity.get());
            writePrimitive(w, QLatin1String(kLayerFrame), item->shape,
                           [&](const QPointF &p) { return flip(p); });
            break;
        }
        case EntityType::Label: {
            const auto *label = static_cast<const Label *>(entity.get());
            writeText(w, QLatin1String(kLayerTags), flip(label->point + QPointF(0.0, -1.5)),
                      label->name, label->height, Primitive::Align::Center);
            break;
        }
        }
    }

    w.code(0, "ENDSEC");
    w.code(0, "EOF");
    w.flush();
    return buffer;
}

bool DxfExport::writeFolio(const QString &path, const Project &project, const Folio &folio,
                           const DxfExportOptions &options, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray payload = encodeFolio(project, folio, options);
    if (file.write(payload) != payload.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

int DxfExport::writeProject(const QString &directory, const QString &baseName,
                            const Project &project, const DxfExportOptions &options,
                            QStringList *errors)
{
    QDir().mkpath(directory);
    int written = 0;
    const auto folios = project.folios();
    for (int i = 0; i < int(folios.size()); ++i) {
        const Folio *folio = folios[std::size_t(i)];
        const QString tag = folio->number.isEmpty() ? QString::number(i + 1) : folio->number;
        // sanitizeName sert aux noms de bloc DXF, pas aux noms de fichier :
        // elle prefixerait « 1 » en « B1 ». Ici il suffit d'ecarter les
        // caracteres interdits par le systeme de fichiers.
        QString safeTag = tag;
        safeTag.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),
                        QStringLiteral("_"));
        const QString path = QDir(directory).filePath(
                QStringLiteral("%1-%2.dxf").arg(baseName, safeTag));
        QString error;
        if (writeFolio(path, project, *folio, options, &error))
            ++written;
        else if (errors)
            errors->append(QStringLiteral("%1 : %2").arg(path, error));
    }
    return written;
}

} // namespace dsn
