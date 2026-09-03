#include "entities.h"
#include "jsonutils.h"

namespace dsn {

namespace {

QRectF boundsOf(const QVector<QPointF> &points, double margin = 0.0)
{
    if (points.isEmpty())
        return QRectF();
    QRectF r(points.first(), points.first());
    for (const QPointF &p : points) {
        r.setLeft(std::min(r.left(), p.x()));
        r.setTop(std::min(r.top(), p.y()));
        r.setRight(std::max(r.right(), p.x()));
        r.setBottom(std::max(r.bottom(), p.y()));
    }
    return margin > 0.0 ? r.adjusted(-margin, -margin, margin, margin) : r;
}

void translatePoints(QVector<QPointF> &points, const QPointF &delta)
{
    for (QPointF &p : points)
        p += delta;
}

} // namespace

// --------------------------------------------------------------------------
// SymbolInstance

EntityPtr SymbolInstance::clone() const { return std::make_unique<SymbolInstance>(*this); }

bool SymbolInstance::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const SymbolInstance *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF SymbolInstance::boundingBox() const { return placement.mapRect(m_localBounds); }

void SymbolInstance::translate(const QPointF &delta) { placement.position += delta; }

void SymbolInstance::scale(const QPointF &base, double factor)
{
    if (fuzzyEqual(factor, 1.0) || factor <= kEpsilon)
        return;
    // Le symbole ne redessine pas son graphisme : il porte un facteur, et le
    // peintre comme la connectivite passent par le meme placement. Les
    // broches suivent donc sans que personne d'autre l'apprenne.
    placement.position = scaledAbout(placement.position, base, factor);
    placement.scale *= factor;
}

QJsonObject SymbolInstance::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("def")] = definitionId;
    o[QStringLiteral("placement")] = placementToJson(placement);
    if (!fields.isEmpty())
        o[QStringLiteral("fields")] = stringMapToJson(fields);
    if (!deviceGroup.isEmpty()) {
        o[QStringLiteral("group")] = deviceGroup;
        o[QStringLiteral("block")] = blockIndex;
    }
    if (designationLocked)
        o[QStringLiteral("designationLocked")] = true;
    return o;
}

bool SymbolInstance::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    definitionId = object.value(QStringLiteral("def")).toString();
    placement = placementFromJson(object.value(QStringLiteral("placement")));
    fields = stringMapFromJson(object.value(QStringLiteral("fields")));
    deviceGroup = object.value(QStringLiteral("group")).toString();
    blockIndex = object.value(QStringLiteral("block")).toInt(0);
    designationLocked = object.value(QStringLiteral("designationLocked")).toBool(false);
    return !definitionId.isEmpty();
}

// --------------------------------------------------------------------------
// Wire

EntityPtr Wire::clone() const { return std::make_unique<Wire>(*this); }

bool Wire::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const Wire *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF Wire::boundingBox() const { return boundsOf(points, 0.5); }

void Wire::translate(const QPointF &delta) { translatePoints(points, delta); }

void Wire::scale(const QPointF &base, double factor)
{
    if (fuzzyEqual(factor, 1.0) || factor <= kEpsilon)
        return;
    for (QPointF &p : points)
        p = scaledAbout(p, base, factor);
}

QString Wire::conductorName(int index) const
{
    if (conductors.isEmpty())
        return QString();
    if (index < 0 || index >= conductors.size())
        return QString();
    return conductors.at(index);
}

double Wire::length() const
{
    double total = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        const QPointF d = points.at(i) - points.at(i - 1);
        total += std::hypot(d.x(), d.y());
    }
    return total;
}

bool Wire::isDegenerate() const { return points.size() < 2 || length() <= kConnectTolerance; }

QJsonObject Wire::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("points")] = pointsToJson(points);
    if (!conductors.isEmpty())
        o[QStringLiteral("conductors")] = stringListToJson(conductors);
    if (!number.isEmpty())
        o[QStringLiteral("number")] = number;
    if (numberLocked)
        o[QStringLiteral("numberLocked")] = true;
    if (!style.isEmpty())
        o[QStringLiteral("style")] = style;
    if (!wireType.isEmpty())
        o[QStringLiteral("wireType")] = wireType;
    return o;
}

bool Wire::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    points = pointsFromJson(object.value(QStringLiteral("points")));
    conductors = stringListFromJson(object.value(QStringLiteral("conductors")));
    number = object.value(QStringLiteral("number")).toString();
    numberLocked = object.value(QStringLiteral("numberLocked")).toBool(false);
    style = object.value(QStringLiteral("style")).toString();
    wireType = object.value(QStringLiteral("wireType")).toString();
    return points.size() >= 2;
}

// --------------------------------------------------------------------------
// Junction

EntityPtr Junction::clone() const { return std::make_unique<Junction>(*this); }

bool Junction::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const Junction *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF Junction::boundingBox() const
{
    const double r = diameter / 2.0;
    return QRectF(point.x() - r, point.y() - r, diameter, diameter);
}

void Junction::translate(const QPointF &delta) { point += delta; }

void Junction::scale(const QPointF &base, double factor)
{
    if (fuzzyEqual(factor, 1.0) || factor <= kEpsilon)
        return;
    point = scaledAbout(point, base, factor);
    diameter *= factor;
}

QJsonObject Junction::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("at")] = pointToJson(point);
    if (!fuzzyEqual(diameter, 1.2))
        o[QStringLiteral("diameter")] = roundStorage(diameter);
    return o;
}

bool Junction::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    point = pointFromJson(object.value(QStringLiteral("at")));
    diameter = object.value(QStringLiteral("diameter")).toDouble(1.2);
    return true;
}

// --------------------------------------------------------------------------
// TextItem

EntityPtr TextItem::clone() const { return std::make_unique<TextItem>(*this); }

bool TextItem::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const TextItem *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF TextItem::boundingBox() const
{
    // Estimation sans metrique de police : le coeur ne charge pas de police.
    // La couche d'affichage recalcule la boite exacte quand elle en a une.
    const double w = height * 0.62 * std::max(1, int(text.size()));
    QRectF local(0, -height, w, height * 1.35);
    switch (align) {
    case Align::Center: local.moveLeft(-w / 2.0); break;
    case Align::Right: local.moveLeft(-w); break;
    case Align::Left: break;
    }
    return placement.mapRect(local);
}

void TextItem::translate(const QPointF &delta) { placement.position += delta; }

void TextItem::scale(const QPointF &base, double factor)
{
    if (fuzzyEqual(factor, 1.0) || factor <= kEpsilon)
        return;
    // C'est la hauteur de capitale qui grossit, pas le facteur du placement :
    // les deux se multiplieraient, et le texte doublerait au carre.
    placement.position = scaledAbout(placement.position, base, factor);
    height *= factor;
}

QJsonObject TextItem::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("placement")] = placementToJson(placement);
    o[QStringLiteral("height")] = roundStorage(height);
    if (align != Align::Left)
        o[QStringLiteral("align")] = align == Align::Center ? QStringLiteral("center")
                                                            : QStringLiteral("right");
    return o;
}

bool TextItem::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    text = object.value(QStringLiteral("text")).toString();
    placement = placementFromJson(object.value(QStringLiteral("placement")));
    height = object.value(QStringLiteral("height")).toDouble(2.5);
    const QString a = object.value(QStringLiteral("align")).toString();
    align = a == QLatin1String("center") ? Align::Center
          : a == QLatin1String("right")  ? Align::Right
                                         : Align::Left;
    return true;
}

// --------------------------------------------------------------------------
// GraphicItem

EntityPtr GraphicItem::clone() const { return std::make_unique<GraphicItem>(*this); }

bool GraphicItem::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const GraphicItem *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF GraphicItem::boundingBox() const { return shape.bounds(); }

void GraphicItem::translate(const QPointF &delta) { shape.translate(delta); }

void GraphicItem::scale(const QPointF &base, double factor) { shape.scale(base, factor); }

QJsonObject GraphicItem::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("shape")] = shape.toJson();
    return o;
}

bool GraphicItem::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    shape = Primitive::fromJson(object.value(QStringLiteral("shape")));
    return true;
}

// --------------------------------------------------------------------------
// Label

EntityPtr Label::clone() const { return std::make_unique<Label>(*this); }

bool Label::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const Label *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

QRectF Label::boundingBox() const
{
    const double w = height * 0.62 * std::max(1, int(name.size())) + height;
    return QRectF(point.x() - w / 2.0, point.y() - height, w, height * 2.0);
}

void Label::translate(const QPointF &delta) { point += delta; }

void Label::scale(const QPointF &base, double factor)
{
    if (fuzzyEqual(factor, 1.0) || factor <= kEpsilon)
        return;
    point = scaledAbout(point, base, factor);
    height *= factor;
}

QJsonObject Label::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("at")] = pointToJson(point);
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("dir")] = toDegrees(direction);
    o[QStringLiteral("scope")] = scope == Scope::Project ? QStringLiteral("project")
                                                         : QStringLiteral("folio");
    o[QStringLiteral("height")] = roundStorage(height);
    if (role != Role::Plain)
        o[QStringLiteral("role")] = role == Role::Source ? QStringLiteral("source")
                                                        : QStringLiteral("destination");
    return o;
}

bool Label::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    point = pointFromJson(object.value(QStringLiteral("at")));
    name = object.value(QStringLiteral("name")).toString();
    direction = directionFromDegrees(object.value(QStringLiteral("dir")).toInt(0));
    scope = object.value(QStringLiteral("scope")).toString() == QLatin1String("project")
            ? Scope::Project
            : Scope::Folio;
    height = object.value(QStringLiteral("height")).toDouble(2.0);
    const QString roleText = object.value(QStringLiteral("role")).toString();
    if (roleText == QLatin1String("source"))
        role = Role::Source;
    else if (roleText == QLatin1String("destination"))
        role = Role::Destination;
    else
        role = Role::Plain;
    // Une fleche de signal est inter-folios par construction : un fichier qui
    // dirait le contraire serait muet a la relecture.
    if (role != Role::Plain)
        scope = Scope::Project;
    return !name.isEmpty();
}

// --------------------------------------------------------------------------
// Cotation

EntityPtr DimensionItem::clone() const { return std::make_unique<DimensionItem>(*this); }

bool DimensionItem::assign(const Entity &other)
{
    const auto *typed = dynamic_cast<const DimensionItem *>(&other);
    if (!typed)
        return false;
    *this = *typed;
    return true;
}

double DimensionItem::measure() const
{
    switch (kind) {
    case Kind::Horizontal:
        return std::abs(second.x() - first.x());
    case Kind::Vertical:
        return std::abs(second.y() - first.y());
    case Kind::Aligned:
        break;
    }
    return std::hypot(second.x() - first.x(), second.y() - first.y());
}

QString DimensionItem::displayText() const
{
    // La valeur imposee gagne, et c'est le seul cas ou une cote ne dit pas ce
    // qu'elle mesure : une rupture d'echelle se declare a la main.
    if (!override.isEmpty())
        return override;
    QString texte = QString::number(measure(), 'f', std::max(0, decimals));
    if (!suffix.isEmpty())
        texte += QLatin1Char(' ') + suffix;
    return texte;
}

DimensionItem::Geometry DimensionItem::geometry() const
{
    Geometry g;
    g.value = measure();

    // La direction de la mesure. Horizontale et verticale ne mesurent qu'une
    // projection : c'est ce qui permet de coter l'entraxe de deux rails sans
    // avoir designe deux points exactement a la meme hauteur.
    QPointF direction;
    switch (kind) {
    case Kind::Horizontal:
        direction = QPointF(1.0, 0.0);
        break;
    case Kind::Vertical:
        direction = QPointF(0.0, 1.0);
        break;
    case Kind::Aligned: {
        direction = second - first;
        const double norme = std::hypot(direction.x(), direction.y());
        direction = norme < kEpsilon ? QPointF(1.0, 0.0) : direction / norme;
        break;
    }
    }
    // La normale, du cote ou le troisieme clic a pose la ligne.
    const QPointF normale(-direction.y(), direction.x());

    // Projection des deux attaches sur la droite de cote : elle passe par
    // linePoint et porte la direction. Donner le decalage par un POINT plutot
    // que par une distance signee evite d'avoir a penser au signe.
    auto surLaLigne = [&](const QPointF &p) {
        const QPointF v = p - linePoint;
        const double le_long = v.x() * direction.x() + v.y() * direction.y();
        return linePoint + direction * le_long;
    };
    g.lineStart = surLaLigne(first);
    g.lineEnd = surLaLigne(second);

    // Les lignes d'attache partent du point mesure et depassent legerement la
    // ligne de cote : c'est la convention de dessin, et le depassement dit que
    // la cote s'arrete la, pas que le trait continue.
    // Et elles partent a un millimetre du point mesure, jamais dessus : une
    // ligne d'attache collee a la piece se confond avec elle, et sur un
    // schema dense on ne sait plus si le trait est une cote ou un fil.
    constexpr double depassement = 1.5;
    constexpr double jeu = 1.0;
    auto attache = [&](const QPointF &mesure, const QPointF &surLigne, QPointF &from,
                       QPointF &to) {
        from = mesure;
        to = surLigne;
        const QPointF sens = surLigne - mesure;
        const double n = std::hypot(sens.x(), sens.y());
        if (n <= jeu + kEpsilon)
            return; // trop court pour un jeu : on garde le trait entier
        const QPointF unite = sens / n;
        from = mesure + unite * jeu;
        to = surLigne + unite * depassement;
    };
    attache(first, g.lineStart, g.firstFrom, g.firstTo);
    attache(second, g.lineEnd, g.secondFrom, g.secondTo);

    g.textAt = (g.lineStart + g.lineEnd) / 2.0;
    // Le texte suit la cote, mais jamais la tete en bas : au-dela du quart de
    // tour on retourne, sinon une cote verticale se lit de droite a gauche.
    // Une cote verticale se lit du BAS vers le haut, comme le veut l'ISO 129 :
    // on la lit en tournant la planche d'un quart de tour a droite, jamais a
    // gauche. D'ou l'intervalle demi-ouvert [-90, 90) et non (-90, 90].
    double angle = std::atan2(direction.y(), direction.x()) * 180.0 / 3.14159265358979323846;
    if (angle >= 90.0)
        angle -= 180.0;
    if (angle < -90.0)
        angle += 180.0;
    g.angleDegrees = angle;
    return g;
}

QRectF DimensionItem::boundingBox() const
{
    const Geometry g = geometry();
    QRectF box = QRectF(first, second).normalized();
    box = box.united(QRectF(g.lineStart, g.lineEnd).normalized());
    box = box.united(QRectF(g.firstTo, g.secondTo).normalized());
    // De quoi contenir le texte pose au-dessus de la ligne.
    return box.adjusted(-textHeight, -textHeight * 1.5, textHeight, textHeight * 1.5);
}

void DimensionItem::translate(const QPointF &delta)
{
    first += delta;
    second += delta;
    linePoint += delta;
}

void DimensionItem::scale(const QPointF &base, double factor)
{
    if (fuzzyEqual(factor, 1.0) || factor <= kEpsilon)
        return;
    first = scaledAbout(first, base, factor);
    second = scaledAbout(second, base, factor);
    linePoint = scaledAbout(linePoint, base, factor);
    // La hauteur du texte grandit, comme celle d'un texte pose : ce qui ne
    // suit pas l'echelle, c'est l'epaisseur du trait, jamais la dimension.
    textHeight *= factor;
    // La VALEUR suit toute seule : elle se mesure. C'est exactement ce qu'on
    // attend d'une cote, et ce qu'un texte pose a cote ne saurait pas faire.
}

QString DimensionItem::kindTag(Kind kind)
{
    switch (kind) {
    case Kind::Horizontal: return QStringLiteral("horizontal");
    case Kind::Vertical: return QStringLiteral("vertical");
    case Kind::Aligned: break;
    }
    return QStringLiteral("aligned");
}

DimensionItem::Kind DimensionItem::kindFromTag(const QString &tag)
{
    if (tag == QLatin1String("horizontal"))
        return Kind::Horizontal;
    if (tag == QLatin1String("vertical"))
        return Kind::Vertical;
    return Kind::Aligned;
}

QJsonObject DimensionItem::toJson() const
{
    QJsonObject o = Entity::toJson();
    o[QStringLiteral("a")] = pointToJson(first);
    o[QStringLiteral("b")] = pointToJson(second);
    o[QStringLiteral("line")] = pointToJson(linePoint);
    o[QStringLiteral("kind")] = kindTag(kind);
    o[QStringLiteral("height")] = roundStorage(textHeight);
    if (decimals != 0)
        o[QStringLiteral("decimals")] = decimals;
    if (!suffix.isEmpty())
        o[QStringLiteral("suffix")] = suffix;
    if (!override.isEmpty())
        o[QStringLiteral("override")] = override;
    return o;
}

bool DimensionItem::readJson(const QJsonObject &object)
{
    Entity::readJson(object);
    first = pointFromJson(object.value(QStringLiteral("a")));
    second = pointFromJson(object.value(QStringLiteral("b")));
    linePoint = pointFromJson(object.value(QStringLiteral("line")));
    kind = kindFromTag(object.value(QStringLiteral("kind")).toString());
    textHeight = object.value(QStringLiteral("height")).toDouble(2.5);
    decimals = object.value(QStringLiteral("decimals")).toInt(0);
    suffix = object.value(QStringLiteral("suffix")).toString();
    override = object.value(QStringLiteral("override")).toString();
    // Une cote dont les deux attaches sont confondues ne mesure rien : elle
    // serait invisible et impossible a rattraper.
    return !samePoint(first, second);
}

} // namespace dsn
