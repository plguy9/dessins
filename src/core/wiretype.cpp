#include "wiretype.h"

#include <QJsonArray>

#include <algorithm>

namespace dsn {

QString WireType::colorName() const
{
    return QStringLiteral("#%1").arg(rgb & 0xFFFFFFu, 6, 16, QLatin1Char('0'));
}

void WireType::setColorName(const QString &text)
{
    QString hex = text.trimmed();
    if (hex.startsWith(QLatin1Char('#')))
        hex.remove(0, 1);
    if (hex.size() != 6)
        return;
    bool ok = false;
    const uint value = hex.toUInt(&ok, 16);
    if (ok)
        rgb = quint32(value);
}

QJsonObject WireType::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("color")] = colorName();
    o[QStringLiteral("width")] = width;
    if (!crossSection.isEmpty())
        o[QStringLiteral("crossSection")] = crossSection;
    if (!layer.isEmpty())
        o[QStringLiteral("layer")] = layer;
    if (!style.isEmpty())
        o[QStringLiteral("style")] = style;
    if (!note.isEmpty())
        o[QStringLiteral("note")] = note;
    return o;
}

WireType WireType::fromJson(const QJsonValue &value)
{
    const QJsonObject o = value.toObject();
    WireType t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.name = o.value(QStringLiteral("name")).toString();
    t.setColorName(o.value(QStringLiteral("color")).toString());
    t.width = o.value(QStringLiteral("width")).toDouble(0.35);
    t.crossSection = o.value(QStringLiteral("crossSection")).toString();
    t.layer = o.value(QStringLiteral("layer")).toString();
    t.style = o.value(QStringLiteral("style")).toString();
    t.note = o.value(QStringLiteral("note")).toString();
    return t;
}

// --------------------------------------------------------------------------

WireTypeSet::WireTypeSet()
{
    WireType fallback;
    fallback.id = defaultId();
    fallback.name = QStringLiteral("Fil standard");
    fallback.layer = QStringLiteral("FILS");
    m_types.append(fallback);
}

void WireTypeSet::insert(const WireType &type)
{
    if (!type.isValid())
        return;
    for (WireType &existing : m_types) {
        if (existing.id == type.id) {
            existing = type;
            return;
        }
    }
    m_types.append(type);
}

void WireTypeSet::remove(const QString &id)
{
    // Le type par defaut ne se retire pas : c'est le repli de tous les fils
    // dont le type a disparu, et sans lui ils deviendraient introuvables.
    if (id == defaultId())
        return;
    m_types.removeIf([&](const WireType &t) { return t.id == id; });
}

void WireTypeSet::clear()
{
    m_types.clear();
    *this = WireTypeSet();
}

bool WireTypeSet::contains(const QString &id) const
{
    return std::any_of(m_types.cbegin(), m_types.cend(),
                       [&](const WireType &t) { return t.id == id; });
}

const WireType *WireTypeSet::type(const QString &id) const
{
    for (const WireType &t : m_types) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

const WireType &WireTypeSet::resolve(const QString &id) const
{
    if (const WireType *found = type(id))
        return *found;
    // Un identifiant inconnu ne doit jamais rendre un fil invisible : il
    // retombe sur le type par defaut, qui existe toujours.
    return *type(defaultId());
}

QList<WireType> WireTypeSet::all() const { return m_types; }

WireTypeSet WireTypeSet::forNorm(const QString &norm)
{
    WireTypeSet set;
    const bool ansi = norm.compare(QLatin1String("ANSI"), Qt::CaseInsensitive) == 0;

    auto add = [&](const QString &id, const QString &name, quint32 rgb,
                   const QString &section, const QString &layer) {
        WireType t;
        t.id = id;
        t.name = name;
        t.rgb = rgb;
        t.crossSection = section;
        t.layer = layer;
        set.insert(t);
    };

    if (ansi) {
        // Usage nord-americain : noir pour les phases, blanc pour le neutre,
        // vert pour la terre, rouge pour la commande alternative.
        add(QStringLiteral("l1"), QStringLiteral("L1 — phase"), 0x202020u,
            QStringLiteral("12 AWG"), QStringLiteral("FILS_L1"));
        add(QStringLiteral("l2"), QStringLiteral("L2 — phase"), 0x8B1A1Au,
            QStringLiteral("12 AWG"), QStringLiteral("FILS_L2"));
        add(QStringLiteral("l3"), QStringLiteral("L3 — phase"), 0x1C398Eu,
            QStringLiteral("12 AWG"), QStringLiteral("FILS_L3"));
        add(QStringLiteral("n"), QStringLiteral("N — neutre"), 0x9E9E9Eu,
            QStringLiteral("12 AWG"), QStringLiteral("FILS_N"));
        add(QStringLiteral("pe"), QStringLiteral("G — terre"), 0x2E7D32u,
            QStringLiteral("12 AWG"), QStringLiteral("FILS_PE"));
        add(QStringLiteral("control"), QStringLiteral("Commande 120 V"), 0xC0392Bu,
            QStringLiteral("16 AWG"), QStringLiteral("FILS_CMD"));
    } else {
        // Couleurs de conducteurs CEI : brun, noir, gris pour les phases,
        // bleu pour le neutre, vert-jaune pour la terre.
        add(QStringLiteral("l1"), QStringLiteral("L1 — phase 1"), 0x7A4A2Bu,
            QStringLiteral("2,5 mm²"), QStringLiteral("FILS_L1"));
        add(QStringLiteral("l2"), QStringLiteral("L2 — phase 2"), 0x2B2B2Bu,
            QStringLiteral("2,5 mm²"), QStringLiteral("FILS_L2"));
        add(QStringLiteral("l3"), QStringLiteral("L3 — phase 3"), 0x8A8A8Au,
            QStringLiteral("2,5 mm²"), QStringLiteral("FILS_L3"));
        add(QStringLiteral("n"), QStringLiteral("N — neutre"), 0x1E6FC4u,
            QStringLiteral("2,5 mm²"), QStringLiteral("FILS_N"));
        add(QStringLiteral("pe"), QStringLiteral("PE — terre"), 0x6E8B1Eu,
            QStringLiteral("2,5 mm²"), QStringLiteral("FILS_PE"));
        add(QStringLiteral("control"), QStringLiteral("Commande 230 V"), 0x0A5C9Eu,
            QStringLiteral("1,5 mm²"), QStringLiteral("FILS_CMD"));
        add(QStringLiteral("dc24"), QStringLiteral("24 V continu"), 0xC0392Bu,
            QStringLiteral("1 mm²"), QStringLiteral("FILS_24V"));
    }
    return set;
}

QJsonArray WireTypeSet::toJson() const
{
    QJsonArray a;
    for (const WireType &t : m_types)
        a.append(t.toJson());
    return a;
}

void WireTypeSet::readJson(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    if (a.isEmpty())
        return;
    m_types.clear();
    for (const QJsonValue &item : a) {
        const WireType t = WireType::fromJson(item);
        if (t.isValid())
            m_types.append(t);
    }
    // Le type par defaut est reconstruit s'il manquait dans le fichier :
    // c'est le repli, il ne peut pas ne pas exister.
    if (!contains(defaultId())) {
        WireType fallback;
        fallback.id = defaultId();
        fallback.name = QStringLiteral("Fil standard");
        fallback.layer = QStringLiteral("FILS");
        m_types.prepend(fallback);
    }
}

} // namespace dsn
