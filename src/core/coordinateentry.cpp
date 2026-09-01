#include "coordinateentry.h"

#include "geometry.h"

#include <cmath>
#include <numbers>

namespace dsn {

namespace {

// Un nombre, avec le point pour decimale. La virgule est reservee au
// separateur de coordonnees : c'est la convention de toute la CAO, et la
// respecter evite qu'une cote veuille dire deux choses.
std::optional<double> number(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    bool ok = false;
    const double value = trimmed.toDouble(&ok);
    if (!ok)
        return std::nullopt;
    return value;
}

} // namespace

bool CoordinateEntry::startsEntry(const QString &text)
{
    if (text.isEmpty())
        return false;
    const QChar c = text.at(0);
    // Un chiffre, un signe, un point : c'est une cote qui commence. Le @ et
    // le # annoncent explicitement un relatif ou un absolu.
    return c.isDigit() || c == QLatin1Char('@') || c == QLatin1Char('#')
            || c == QLatin1Char('-') || c == QLatin1Char('.');
}

double CoordinateEntry::screenAngle(const QPointF &delta)
{
    double angle = -std::atan2(delta.y(), delta.x()) * 180.0 / std::numbers::pi;
    if (angle < 0.0)
        angle += 360.0;
    return angle;
}

QPointF CoordinateEntry::directionOfAngle(double degrees)
{
    const double radians = degrees * std::numbers::pi / 180.0;
    // Le signe de l'ordonnee est inverse : l'angle se lit a l'endroit, l'ecran
    // compte a l'envers.
    return QPointF(std::cos(radians), -std::sin(radians));
}

std::optional<ParsedCoordinate> CoordinateEntry::parse(const QString &text)
{
    QString body = text.trimmed();
    if (body.isEmpty())
        return std::nullopt;

    ParsedCoordinate out;
    out.kind = ParsedCoordinate::Kind::Relative;

    if (body.startsWith(QLatin1Char('#'))) {
        out.kind = ParsedCoordinate::Kind::Absolute;
        body.remove(0, 1);
    } else if (body.startsWith(QLatin1Char('@'))) {
        body.remove(0, 1);
    }
    if (body.isEmpty())
        return std::nullopt;

    // Polaire : longueur < angle.
    const int less = body.indexOf(QLatin1Char('<'));
    if (less >= 0) {
        const auto length = number(body.left(less));
        const auto angle = number(body.mid(less + 1));
        if (!length || !angle)
            return std::nullopt;
        // Un absolu polaire se mesure depuis l'origine du folio ; c'est rare
        // et ambigu, on le traite comme un relatif, comme le fait AutoCAD en
        // saisie dynamique.
        out.kind = ParsedCoordinate::Kind::Polar;
        out.first = *length;
        out.second = *angle;
        return out;
    }

    // Cartesien : x , y — le point-virgule est accepte pour qui prefere
    // ecrire ses decimales avec la virgule.
    int separator = body.indexOf(QLatin1Char(';'));
    if (separator < 0)
        separator = body.indexOf(QLatin1Char(','));
    if (separator >= 0) {
        const auto x = number(body.left(separator));
        const auto y = number(body.mid(separator + 1));
        if (!x || !y)
            return std::nullopt;
        out.first = *x;
        out.second = *y;
        return out;
    }

    // Un nombre seul : une distance dans la direction visee. C'est la forme
    // la plus utilisee de toutes.
    const auto distance = number(body);
    if (!distance)
        return std::nullopt;
    if (out.kind == ParsedCoordinate::Kind::Absolute)
        return std::nullopt; // « #50 » ne designe rien
    out.kind = ParsedCoordinate::Kind::Distance;
    out.first = *distance;
    return out;
}

std::optional<QPointF> CoordinateEntry::resolve(const ParsedCoordinate &parsed,
                                                const QPointF *from, const QPointF &aim)
{
    if (parsed.kind == ParsedCoordinate::Kind::Absolute)
        return QPointF(parsed.first, parsed.second);

    if (!from)
        return std::nullopt;

    switch (parsed.kind) {
    case ParsedCoordinate::Kind::Relative:
        // L'ordonnee se compte vers le bas a l'ecran mais se saisit vers le
        // haut, comme sur un plan cote : « 0,10 » monte de dix millimetres.
        return *from + QPointF(parsed.first, -parsed.second);
    case ParsedCoordinate::Kind::Polar:
        return *from + directionOfAngle(parsed.second) * parsed.first;
    case ParsedCoordinate::Kind::Distance: {
        QPointF direction = aim - *from;
        const double length = std::hypot(direction.x(), direction.y());
        // Sans direction visee, une distance seule ne designe rien : il n'y a
        // pas de « par defaut » raisonnable, et en inventer un ferait poser le
        // point ailleurs qu'ou l'utilisateur regarde.
        if (length < kEpsilon)
            return std::nullopt;
        direction /= length;
        return *from + direction * parsed.first;
    }
    case ParsedCoordinate::Kind::Absolute:
        break;
    }
    return std::nullopt;
}

std::optional<QPointF> CoordinateEntry::resolve(const QString &text, const QPointF *from,
                                                const QPointF &aim)
{
    const auto parsed = parse(text);
    if (!parsed)
        return std::nullopt;
    return resolve(*parsed, from, aim);
}

} // namespace dsn
