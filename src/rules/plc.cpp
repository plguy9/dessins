#include "plc.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>

#include <algorithm>

namespace dsn {

namespace {

// Le pas vertical entre deux points, en millimetres. Multiple du module de
// 2,5 mm de la bibliotheque : un module d'automate doit s'accrocher a la
// grille comme n'importe quel symbole.
constexpr double kPointPitch = 5.0;
// La largeur du corps. Assez pour ecrire « Local:3:I.Data.15 » a 1,8 mm de
// haut sans deborder : l'adresse est la raison d'etre du symbole.
constexpr double kBodyWidth = 34.0;
constexpr double kHeaderHeight = 7.0;
constexpr double kPinLength = 5.0;
constexpr double kAddressHeight = 1.8;

} // namespace

// --------------------------------------------------------------------------
// PlcModuleDef

bool PlcModuleDef::isOutput() const { return ioType.startsWith(QLatin1String("sortie")); }

bool PlcModuleDef::isAnalog() const { return ioType.contains(QLatin1String("analogique")); }

QString PlcModuleDef::searchText() const
{
    return QStringList{ manufacturer, series, partNumber, description, ioType, voltage }
            .join(QLatin1Char(' '));
}

QJsonObject PlcModuleDef::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("manufacturer")] = manufacturer;
    o[QStringLiteral("series")] = series;
    o[QStringLiteral("partNumber")] = partNumber;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("ioType")] = ioType;
    o[QStringLiteral("points")] = points;
    o[QStringLiteral("addressFormat")] = addressFormat;
    if (bitsPerByte > 0)
        o[QStringLiteral("bitsPerByte")] = bitsPerByte;
    if (!voltage.isEmpty())
        o[QStringLiteral("voltage")] = voltage;
    return o;
}

PlcModuleDef PlcModuleDef::fromJson(const QJsonValue &value)
{
    const QJsonObject o = value.toObject();
    PlcModuleDef d;
    d.id = o.value(QStringLiteral("id")).toString();
    d.manufacturer = o.value(QStringLiteral("manufacturer")).toString();
    d.series = o.value(QStringLiteral("series")).toString();
    d.partNumber = o.value(QStringLiteral("partNumber")).toString();
    d.description = o.value(QStringLiteral("description")).toString();
    d.ioType = o.value(QStringLiteral("ioType")).toString();
    d.points = o.value(QStringLiteral("points")).toInt(8);
    d.addressFormat = o.value(QStringLiteral("addressFormat")).toString();
    d.bitsPerByte = o.value(QStringLiteral("bitsPerByte")).toInt(0);
    d.voltage = o.value(QStringLiteral("voltage")).toString();
    return d;
}

// --------------------------------------------------------------------------
// PlcDatabase

void PlcDatabase::insert(const PlcModuleDef &module)
{
    if (!module.isValid())
        return;
    for (PlcModuleDef &existing : m_modules) {
        if (existing.id == module.id) {
            existing = module;
            return;
        }
    }
    m_modules.append(module);
}

const PlcModuleDef *PlcDatabase::find(const QString &id) const
{
    for (const PlcModuleDef &m : m_modules) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

QStringList PlcDatabase::manufacturers() const
{
    QStringList out;
    for (const PlcModuleDef &m : m_modules) {
        if (!m.manufacturer.isEmpty() && !out.contains(m.manufacturer))
            out.append(m.manufacturer);
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QList<PlcModuleDef> PlcDatabase::forManufacturer(const QString &manufacturer) const
{
    if (manufacturer.isEmpty())
        return m_modules;
    QList<PlcModuleDef> out;
    for (const PlcModuleDef &m : m_modules) {
        if (m.manufacturer.compare(manufacturer, Qt::CaseInsensitive) == 0)
            out.append(m);
    }
    return out;
}

QList<PlcModuleDef> PlcDatabase::search(const QString &text, const QString &manufacturer) const
{
    const QList<PlcModuleDef> pool = forManufacturer(manufacturer);
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return pool;
    QList<PlcModuleDef> out;
    for (const PlcModuleDef &m : pool) {
        if (m.searchText().contains(needle, Qt::CaseInsensitive))
            out.append(m);
    }
    return out;
}

bool PlcDatabase::readJson(const QByteArray &json, QString *error)
{
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError) {
        if (error)
            *error = parse.errorString();
        return false;
    }
    const QJsonArray array = document.object().value(QStringLiteral("modules")).toArray();
    for (const QJsonValue &value : array)
        insert(PlcModuleDef::fromJson(value));
    return true;
}

PlcDatabase PlcDatabase::builtin()
{
    PlcDatabase database;
    QFile file(QStringLiteral(":/plc/automates.json"));
    if (file.open(QIODevice::ReadOnly))
        database.readJson(file.readAll());
    return database;
}

QStringList PlcDatabase::userSearchPaths()
{
    QStringList paths;
    for (const QString &base :
         QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)) {
        paths.append(base + QStringLiteral("/automates"));
    }
    return paths;
}

PlcDatabase PlcDatabase::loadAll()
{
    PlcDatabase database = builtin();
    for (const QString &path : userSearchPaths()) {
        QDir dir(path);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files,
                                                QDir::Name);
        for (const QString &name : files) {
            QFile file(dir.filePath(name));
            if (file.open(QIODevice::ReadOnly))
                database.readJson(file.readAll());
        }
    }
    return database;
}

// --------------------------------------------------------------------------
// PlcAddress

QString PlcAddress::format(const QString &pattern, int rack, int slot, int point,
                           int bitsPerByte)
{
    if (pattern.isEmpty())
        return QString::number(point);

    // %B et %b sont deux vues du meme rang, pas une seconde numerotation :
    // un module Siemens de seize points occupe deux octets, et son point 9
    // est le bit 1 de l'octet 1. Sans groupement, l'octet reste zero et le
    // bit vaut le rang — c'est le cas d'Allen-Bradley.
    const int group = bitsPerByte > 0 ? bitsPerByte : 0;
    const int byte = group > 0 ? point / group : 0;
    const int bit = group > 0 ? point % group : point;

    QString out;
    out.reserve(pattern.size() + 8);
    for (int i = 0; i < pattern.size(); ++i) {
        if (pattern.at(i) != QLatin1Char('%')) {
            out.append(pattern.at(i));
            continue;
        }
        if (i + 1 >= pattern.size()) {
            // Un « % » final n'introduit rien : on le rend tel quel plutot
            // que de manger un caractere qui n'existe pas.
            out.append(QLatin1Char('%'));
            break;
        }

        // Largeur facultative : %2P remplit a deux chiffres.
        int width = 0;
        int j = i + 1;
        while (j < pattern.size() && pattern.at(j).isDigit()) {
            width = width * 10 + pattern.at(j).digitValue();
            ++j;
        }
        if (j >= pattern.size()) {
            out.append(pattern.mid(i));
            break;
        }

        const QChar token = pattern.at(j);
        auto pad = [width](int value) {
            return width > 0 ? QStringLiteral("%1").arg(value, width, 10, QLatin1Char('0'))
                             : QString::number(value);
        };
        switch (token.unicode()) {
        case 'R': out.append(pad(rack)); break;
        case 'S': out.append(pad(slot)); break;
        case 'P': out.append(pad(point)); break;
        case 'B': out.append(pad(byte)); break;
        case 'b': out.append(pad(bit)); break;
        case '%': out.append(QLatin1Char('%')); break;
        default:
            // Un jeton inconnu est recopie tel quel, avec son pour-cent : un
            // format de constructeur qu'on ne connait pas encore ne doit pas
            // se transformer en adresse fausse et silencieuse.
            out.append(pattern.mid(i, j - i + 1));
            break;
        }
        i = j;
    }
    return out;
}

// --------------------------------------------------------------------------
// PlcModule

QString PlcModule::descriptionKey(int index)
{
    return QStringLiteral("plc.desc.%1").arg(index);
}

bool PlcModule::isModule(const SymbolInstance &symbol)
{
    return !symbol.fields.value(moduleKey()).isEmpty();
}

QString PlcModule::moduleId(const SymbolInstance &symbol)
{
    return symbol.fields.value(moduleKey());
}

int PlcModule::rack(const SymbolInstance &symbol)
{
    return symbol.fields.value(rackKey()).toInt();
}

int PlcModule::slot(const SymbolInstance &symbol)
{
    return symbol.fields.value(slotKey()).toInt();
}

int PlcModule::firstPoint(const SymbolInstance &symbol)
{
    return symbol.fields.value(firstPointKey()).toInt();
}

void PlcModule::configure(SymbolInstance &symbol, const PlcModuleDef &def, int rack, int slot,
                          int firstPoint)
{
    symbol.fields.insert(moduleKey(), def.id);
    symbol.fields.insert(rackKey(), QString::number(std::max(0, rack)));
    symbol.fields.insert(slotKey(), QString::number(std::max(0, slot)));
    symbol.fields.insert(firstPointKey(), QString::number(std::max(0, firstPoint)));
    // Les donnees catalogue du module sont celles de n'importe quel appareil :
    // la nomenclature doit le commander comme le reste.
    symbol.fields.insert(QStringLiteral("manufacturer"), def.manufacturer);
    symbol.fields.insert(QStringLiteral("partNumber"), def.partNumber);
    if (symbol.fields.value(QStringLiteral("description")).isEmpty())
        symbol.fields.insert(QStringLiteral("description"), def.description);
}

void PlcModule::setDescription(SymbolInstance &symbol, int index, const QString &text)
{
    const QString key = descriptionKey(index);
    if (text.trimmed().isEmpty())
        symbol.fields.remove(key);
    else
        symbol.fields.insert(key, text.trimmed());
}

QString PlcModule::description(const SymbolInstance &symbol, int index)
{
    return symbol.fields.value(descriptionKey(index));
}

QVector<PlcPoint> PlcModule::points(const SymbolInstance &symbol, const PlcDatabase &database)
{
    const PlcModuleDef *def = database.find(moduleId(symbol));
    if (!def)
        return {};

    const int base = firstPoint(symbol);
    QVector<PlcPoint> out;
    out.reserve(def->points);
    for (int i = 0; i < def->points; ++i) {
        PlcPoint p;
        p.index = i;
        p.address = PlcAddress::format(def->addressFormat, rack(symbol), slot(symbol), base + i,
                                       def->bitsPerByte);
        // Le repere de borne du module suit le rang dans le module, pas
        // l'adresse : c'est ce qui est serigraphie sur la carte.
        p.terminal = QStringLiteral("%1").arg(i, 2, 10, QLatin1Char('0'));
        p.pinNumber = p.terminal;
        p.description = description(symbol, i);
        out.append(p);
    }
    return out;
}

QString PlcModule::symbolId(const PlcModuleDef &def)
{
    // La definition engendree porte sa propre norme : elle n'a pas de
    // contrepartie ANSI ou CEI, un module Siemens se dessine pareil partout.
    return QStringLiteral("plc:%1").arg(def.id);
}

SymbolDefinition PlcModule::buildSymbol(const PlcModuleDef &def, const QVector<PlcPoint> &points)
{
    SymbolDefinition symbol;
    symbol.id = symbolId(def);
    symbol.logicalId = def.id;
    symbol.norm = QStringLiteral("PLC");
    symbol.name = def.partNumber.isEmpty() ? def.description : def.partNumber;
    symbol.category = QStringLiteral("Automates");
    symbol.deviceKind = QStringLiteral("plc-module");
    symbol.designationPrefix = QStringLiteral("A");
    symbol.keywords = QStringList{ def.manufacturer, def.series, def.ioType,
                                   QStringLiteral("automate"), QStringLiteral("API") };

    const int count = std::max(1, int(points.size()));
    const double bodyHeight = kHeaderHeight + count * kPointPitch;
    const double top = -bodyHeight / 2.0;
    const double left = -kBodyWidth / 2.0;

    symbol.graphics.append(Primitive::rect(QRectF(left, top, kBodyWidth, bodyHeight), 0.35));
    // Le bandeau de tete : le module se reconnait a sa reference, pas a sa
    // forme — toutes les cartes sont des rectangles.
    symbol.graphics.append(
            Primitive::line(QPointF(left, top + kHeaderHeight),
                            QPointF(left + kBodyWidth, top + kHeaderHeight), 0.35));

    Primitive title;
    title.kind = Primitive::Kind::Text;
    title.text = symbol.name;
    // Une reference constructeur peut faire vingt caracteres : la hauteur du
    // texte se reduit pour tenir dans le bandeau plutot que de deborder de la
    // carte. Le facteur est la largeur moyenne d'un glyphe rapportee a sa
    // hauteur, mesure sur la fonte du peintre.
    constexpr double kGlyphRatio = 0.85;
    const double available = kBodyWidth - 4.0;
    const double needed = std::max(1, int(symbol.name.size())) * kGlyphRatio;
    title.textHeight = std::min(2.2, available / needed);
    title.align = Primitive::Align::Center;
    title.points = { QPointF(0.0, top + kHeaderHeight / 2.0) };
    symbol.graphics.append(title);

    // Les entrees recoivent le cablage a gauche, les sorties le rendent a
    // droite : c'est le sens de lecture d'un folio, de l'amont vers l'aval.
    const bool output = def.isOutput();
    const Direction direction = output ? Direction::Right : Direction::Left;
    const double edge = output ? left + kBodyWidth : left;
    const double outward = output ? kPinLength : -kPinLength;

    for (int i = 0; i < count; ++i) {
        const PlcPoint &point = points.at(i);
        const double y = top + kHeaderHeight + kPointPitch * (i + 0.5);

        Pin pin;
        pin.number = point.terminal;
        pin.name = point.address;
        pin.position = QPointF(edge + outward, y);
        pin.direction = direction;
        pin.length = kPinLength;
        pin.type = output ? PinType::Output : PinType::Input;
        pin.showNumber = true;
        pin.showName = false;
        symbol.pins.append(pin);

        // L'adresse est ecrite dans le corps, du cote oppose au cablage :
        // elle ne doit jamais se retrouver sous un fil.
        Primitive address;
        address.kind = Primitive::Kind::Text;
        address.text = point.address;
        address.textHeight = kAddressHeight;
        address.align = output ? Primitive::Align::Left : Primitive::Align::Right;
        address.points = { QPointF(output ? left + 2.0 : left + kBodyWidth - 2.0, y) };
        symbol.graphics.append(address);
    }

    // Le repere se pose au-dessus de la boite, la valeur en dessous : sur un
    // corps aussi haut, les ancrages par defaut tomberaient dedans.
    symbol.designationAnchor = QPointF(0.0, top - 3.0);
    symbol.valueAnchor = QPointF(0.0, top + bodyHeight + 4.0);
    return symbol;
}

} // namespace dsn
