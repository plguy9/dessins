#pragma once

#include "core/entities.h"
#include "core/project.h"

#include <QVector>

namespace test {

// Symbole a deux broches, horizontales, espacees de 10 mm : le gabarit d'un
// contact, d'une bobine ou d'un fusible pour les besoins des tests.
inline dsn::SymbolDefinition twoPinDevice(const QString &logicalId = QStringLiteral("device"),
                                          const QString &prefix = QStringLiteral("K"))
{
    dsn::SymbolDefinition def;
    def.logicalId = logicalId;
    def.norm = QStringLiteral("IEC");
    def.id = dsn::SymbolDefinition::makeId(def.norm, logicalId);
    def.name = QStringLiteral("Appareil deux bornes");
    def.category = QStringLiteral("Test");
    def.designationPrefix = prefix;
    def.deviceKind = logicalId;
    def.graphics.append(dsn::Primitive::rect(QRectF(-2.5, -2.5, 5.0, 5.0)));

    dsn::Pin p1;
    p1.number = QStringLiteral("1");
    p1.position = QPointF(-5.0, 0.0);
    p1.direction = dsn::Direction::Left;
    p1.length = 2.5;
    def.pins.append(p1);

    dsn::Pin p2;
    p2.number = QStringLiteral("2");
    p2.position = QPointF(5.0, 0.0);
    p2.direction = dsn::Direction::Right;
    p2.length = 2.5;
    def.pins.append(p2);

    return def;
}

inline dsn::SymbolInstance *placeSymbol(dsn::Project &project, dsn::Folio *folio,
                                        const QString &definitionId, const QPointF &at,
                                        const QString &designation = QString())
{
    auto instance = std::make_unique<dsn::SymbolInstance>();
    instance->definitionId = definitionId;
    instance->placement.position = at;
    if (!designation.isEmpty())
        instance->setDesignation(designation);
    if (const dsn::SymbolDefinition *def = project.library.definition(definitionId))
        instance->setLocalBounds(def->bounds());
    auto *raw = instance.get();
    folio->addEntity(std::move(instance));
    return raw;
}

inline dsn::Wire *drawWire(dsn::Folio *folio, const QVector<QPointF> &points,
                           const QStringList &conductors = {})
{
    auto wire = std::make_unique<dsn::Wire>();
    wire->points = points;
    wire->conductors = conductors;
    auto *raw = wire.get();
    folio->addEntity(std::move(wire));
    return raw;
}

inline dsn::Junction *dropJunction(dsn::Folio *folio, const QPointF &at)
{
    auto junction = std::make_unique<dsn::Junction>();
    junction->point = at;
    auto *raw = junction.get();
    folio->addEntity(std::move(junction));
    return raw;
}

inline dsn::Label *dropLabel(dsn::Folio *folio, const QPointF &at, const QString &name,
                             dsn::Label::Scope scope = dsn::Label::Scope::Folio)
{
    auto label = std::make_unique<dsn::Label>();
    label->point = at;
    label->name = name;
    label->scope = scope;
    auto *raw = label.get();
    folio->addEntity(std::move(label));
    return raw;
}

} // namespace test
