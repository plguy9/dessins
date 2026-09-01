#include "netlist.h"
#include "entities.h"

#include <QSet>

#include <algorithm>
#include <numeric>

namespace dsn {

namespace {

// --------------------------------------------------------------------------
// Union-find avec compression de chemin. Le graphe de connexite d'un schema
// est typiquement large et peu profond, cette structure suffit largement.
class DisjointSet
{
public:
    int add()
    {
        m_parent.append(int(m_parent.size()));
        m_rank.append(0);
        return int(m_parent.size()) - 1;
    }

    int find(int x)
    {
        while (m_parent[x] != x) {
            m_parent[x] = m_parent[m_parent[x]];
            x = m_parent[x];
        }
        return x;
    }

    void unite(int a, int b)
    {
        int ra = find(a);
        int rb = find(b);
        if (ra == rb)
            return;
        if (m_rank[ra] < m_rank[rb])
            std::swap(ra, rb);
        m_parent[rb] = ra;
        if (m_rank[ra] == m_rank[rb])
            ++m_rank[ra];
    }

    int size() const { return int(m_parent.size()); }

private:
    QVector<int> m_parent;
    QVector<int> m_rank;
};

// --------------------------------------------------------------------------
// Index spatial. Les positions sont rangees dans des cases de deux fois la
// tolerance, et toute recherche sonde les neuf cases voisines : deux points
// separes par un cheveu tombent parfois de part et d'autre d'une frontiere.
struct Cell {
    qint64 x = 0;
    qint64 y = 0;
    bool operator==(const Cell &o) const noexcept { return x == o.x && y == o.y; }
};

std::size_t qHash(const Cell &c, std::size_t seed = 0) noexcept
{
    return ::qHash(c.x, seed) ^ (::qHash(c.y, seed) << 1);
}

Cell cellOf(const QPointF &p)
{
    const double s = kConnectTolerance * 2.0;
    return Cell{ qint64(std::floor(p.x() / s)), qint64(std::floor(p.y() / s)) };
}

// Nature de ce qui touche un point du schema.
enum class TouchKind { PinTouch, WireEnd, WireBend, JunctionTouch, LabelTouch };

struct Touch {
    TouchKind kind = TouchKind::WireEnd;
    QPointF position;
    const Wire *wire = nullptr;
    QString entityId;
    QString folioId;
    // Un noeud par conducteur pour un fil, un seul noeud sinon.
    QVector<int> nodes;

    bool isTerminal() const { return kind != TouchKind::WireBend; }
};

struct NodeInfo {
    QPointF position;
    QString folioId;
    QString wireId;
    int conductor = 0;
    QString conductorName;
};

} // namespace

// --------------------------------------------------------------------------

class NetlistBuilder
{
public:
    explicit NetlistBuilder(const Project &project) : m_project(project) {}

    Netlist build();

private:
    int newNode(const QPointF &pos, const QString &folioId, const QString &wireId, int conductor,
                const QString &conductorName);
    void collectFolio(const Folio &folio);
    void connectAtSites();
    void connectTaps();
    void applyLabels();
    void assemble(Netlist &out);

    void addTouch(Touch touch);
    // Les coordonnees sont locales au folio : une recherche de voisinage doit
    // rester enfermee dans son folio, sans quoi deux pages qui utilisent le
    // meme point de la feuille se retrouvent electriquement soudees.
    QVector<int> touchesNear(const QPointF &p, const QString &folioId) const;

    // Apparie les conducteurs de deux fils qui se rejoignent : par nom si les
    // deux jeux sont nommes, par rang sinon.
    void uniteWires(const Touch &a, const Touch &b);
    void uniteTerminalWith(const Touch &terminal, const Touch &wireTouch);

    const Project &m_project;
    DisjointSet m_sets;
    QVector<NodeInfo> m_nodes;
    QVector<Touch> m_touches;
    QHash<Cell, QVector<int>> m_grid;

    struct LabelRecord {
        QString name;
        Label::Scope scope = Label::Scope::Folio;
        QString folioId;
        int node = -1;
    };
    QVector<LabelRecord> m_labels;

    struct PinRecord {
        Netlist::PinRef ref;
        int node = -1;
    };
    QVector<PinRecord> m_pins;

    QVector<Netlist::Diagnostic> m_diagnostics;
};

int NetlistBuilder::newNode(const QPointF &pos, const QString &folioId, const QString &wireId,
                            int conductor, const QString &conductorName)
{
    const int id = m_sets.add();
    NodeInfo info;
    info.position = pos;
    info.folioId = folioId;
    info.wireId = wireId;
    info.conductor = conductor;
    info.conductorName = conductorName;
    m_nodes.append(info);
    Q_ASSERT(m_nodes.size() == m_sets.size());
    return id;
}

void NetlistBuilder::addTouch(Touch touch)
{
    const int index = int(m_touches.size());
    m_grid[cellOf(touch.position)].append(index);
    m_touches.append(std::move(touch));
}

QVector<int> NetlistBuilder::touchesNear(const QPointF &p, const QString &folioId) const
{
    QVector<int> out;
    const Cell c = cellOf(p);
    for (qint64 dx = -1; dx <= 1; ++dx) {
        for (qint64 dy = -1; dy <= 1; ++dy) {
            auto it = m_grid.constFind(Cell{ c.x + dx, c.y + dy });
            if (it == m_grid.constEnd())
                continue;
            for (int index : *it) {
                const Touch &touch = m_touches.at(index);
                if (touch.folioId == folioId && samePoint(touch.position, p))
                    out.append(index);
            }
        }
    }
    return out;
}

void NetlistBuilder::collectFolio(const Folio &folio)
{
    // --- fils -----------------------------------------------------------
    for (const Wire *wire : folio.entitiesOfType<Wire>()) {
        if (wire->isDegenerate()) {
            m_diagnostics.append({ Netlist::Diagnostic::Severity::Warning,
                                   QStringLiteral("wire.degenerate"),
                                   QStringLiteral("Fil de longueur nulle"), folio.id(), wire->id(),
                                   wire->start() });
            continue;
        }
        const int conductors = wire->conductorCount();

        // Un noeud par sommet et par conducteur, puis fusion le long du fil :
        // un fil est un seul potentiel par conducteur, quels que soient ses
        // changements de direction.
        QVector<QVector<int>> perVertex;
        perVertex.reserve(wire->points.size());
        for (const QPointF &p : wire->points) {
            QVector<int> nodes;
            nodes.reserve(conductors);
            for (int c = 0; c < conductors; ++c)
                nodes.append(newNode(p, folio.id(), wire->id(), c, wire->conductorName(c)));
            perVertex.append(nodes);
        }
        for (int v = 1; v < perVertex.size(); ++v) {
            for (int c = 0; c < conductors; ++c)
                m_sets.unite(perVertex.at(v - 1).at(c), perVertex.at(v).at(c));
        }

        for (int v = 0; v < perVertex.size(); ++v) {
            Touch t;
            t.kind = (v == 0 || v == perVertex.size() - 1) ? TouchKind::WireEnd
                                                           : TouchKind::WireBend;
            t.position = wire->points.at(v);
            t.wire = wire;
            t.entityId = wire->id();
            t.folioId = folio.id();
            t.nodes = perVertex.at(v);
            addTouch(std::move(t));
        }
    }

    // --- broches de symboles --------------------------------------------
    for (const SymbolInstance *symbol : folio.entitiesOfType<SymbolInstance>()) {
        const SymbolDefinition *def = m_project.library.definition(symbol->definitionId);
        if (!def) {
            m_diagnostics.append({ Netlist::Diagnostic::Severity::Error,
                                   QStringLiteral("symbol.missingDefinition"),
                                   QStringLiteral("Definition de symbole introuvable : ")
                                           + symbol->definitionId,
                                   folio.id(), symbol->id(), symbol->placement.position });
            continue;
        }
        for (const Pin &pin : def->pins) {
            if (pin.type == PinType::NotConnected)
                continue;
            const QPointF absolute = symbol->placement.map(pin.position);
            const int node = newNode(absolute, folio.id(), QString(), 0, QString());

            PinRecord record;
            record.node = node;
            record.ref.folioId = folio.id();
            record.ref.symbolId = symbol->id();
            record.ref.designation = symbol->designation();
            record.ref.pinNumber = pin.number;
            record.ref.definitionId = symbol->definitionId;
            record.ref.type = pin.type;
            record.ref.position = absolute;
            m_pins.append(record);

            Touch t;
            t.kind = TouchKind::PinTouch;
            t.position = absolute;
            t.entityId = symbol->id();
            t.folioId = folio.id();
            t.nodes = { node };
            addTouch(std::move(t));
        }
    }

    // --- jonctions -------------------------------------------------------
    for (const Junction *junction : folio.entitiesOfType<Junction>()) {
        const int node = newNode(junction->point, folio.id(), QString(), 0, QString());
        Touch t;
        t.kind = TouchKind::JunctionTouch;
        t.position = junction->point;
        t.entityId = junction->id();
        t.folioId = folio.id();
        t.nodes = { node };
        addTouch(std::move(t));
    }

    // --- etiquettes et renvois ------------------------------------------
    for (const Label *label : folio.entitiesOfType<Label>()) {
        const int node = newNode(label->point, folio.id(), QString(), 0, QString());
        Touch t;
        t.kind = TouchKind::LabelTouch;
        t.position = label->point;
        t.entityId = label->id();
        t.folioId = folio.id();
        t.nodes = { node };
        addTouch(std::move(t));

        LabelRecord record;
        record.name = label->name;
        record.scope = label->scope;
        record.folioId = folio.id();
        record.node = node;
        m_labels.append(record);
    }
}

void NetlistBuilder::uniteWires(const Touch &a, const Touch &b)
{
    const bool namedA = a.wire && !a.wire->conductors.isEmpty();
    const bool namedB = b.wire && !b.wire->conductors.isEmpty();

    if (namedA && namedB) {
        // Appariement par nom : l'ordre de saisie des conducteurs d'un cable
        // n'a pas a etre le meme des deux cotes d'une borne.
        for (int i = 0; i < a.nodes.size(); ++i) {
            const QString name = a.wire->conductorName(i);
            for (int j = 0; j < b.nodes.size(); ++j) {
                if (b.wire->conductorName(j) == name) {
                    m_sets.unite(a.nodes.at(i), b.nodes.at(j));
                    break;
                }
            }
        }
        return;
    }

    const int common = std::min(a.nodes.size(), b.nodes.size());
    for (int i = 0; i < common; ++i)
        m_sets.unite(a.nodes.at(i), b.nodes.at(i));
}

void NetlistBuilder::uniteTerminalWith(const Touch &terminal, const Touch &wireTouch)
{
    if (terminal.nodes.isEmpty() || wireTouch.nodes.isEmpty())
        return;
    // Un element mono-conducteur s'attache au premier conducteur de la liaison.
    m_sets.unite(terminal.nodes.first(), wireTouch.nodes.first());
}

void NetlistBuilder::connectAtSites()
{
    QSet<int> visited;
    for (int i = 0; i < int(m_touches.size()); ++i) {
        if (visited.contains(i))
            continue;
        const QVector<int> site =
                touchesNear(m_touches.at(i).position, m_touches.at(i).folioId);
        for (int index : site)
            visited.insert(index);
        if (site.size() < 2)
            continue;

        bool hasJunction = false;
        for (int index : site) {
            if (m_touches.at(index).kind == TouchKind::JunctionTouch) {
                hasJunction = true;
                break;
            }
        }

        for (int a = 0; a < site.size(); ++a) {
            for (int b = a + 1; b < site.size(); ++b) {
                const Touch &ta = m_touches.at(site.at(a));
                const Touch &tb = m_touches.at(site.at(b));

                // Deux fils qui se croisent sans jonction ne connectent pas,
                // meme si chacun possede un sommet au croisement.
                if (!hasJunction && !ta.isTerminal() && !tb.isTerminal())
                    continue;

                if (ta.wire && tb.wire)
                    uniteWires(ta, tb);
                else if (ta.wire)
                    uniteTerminalWith(tb, ta);
                else if (tb.wire)
                    uniteTerminalWith(ta, tb);
                else if (!ta.nodes.isEmpty() && !tb.nodes.isEmpty())
                    m_sets.unite(ta.nodes.first(), tb.nodes.first());
            }
        }
    }
}

void NetlistBuilder::connectTaps()
{
    // Piquage en T : une extremite (fil, broche, jonction, etiquette) posee au
    // milieu d'un segment d'un autre fil. Les sommets ont deja ete traites par
    // connectAtSites ; il ne reste que les points strictement interieurs.
    for (int t = 0; t < int(m_touches.size()); ++t) {
        const Touch &terminal = m_touches.at(t);
        if (!terminal.isTerminal())
            continue;

        for (int w = 0; w < int(m_touches.size()); ++w) {
            const Touch &wireTouch = m_touches.at(w);
            if (!wireTouch.wire || wireTouch.entityId == terminal.entityId)
                continue;
            if (wireTouch.folioId != terminal.folioId)
                continue;
            // Un seul sommet par fil suffit a le representer ici.
            if (wireTouch.wire->points.isEmpty()
                || !samePoint(wireTouch.position, wireTouch.wire->points.first()))
                continue;

            const Wire *wire = wireTouch.wire;
            bool onVertex = false;
            for (const QPointF &v : wire->points) {
                if (samePoint(v, terminal.position)) {
                    onVertex = true;
                    break;
                }
            }
            if (onVertex)
                continue; // deja pris en charge comme site

            bool onSegment = false;
            for (int s = 1; s < wire->points.size(); ++s) {
                if (pointOnSegment(terminal.position, wire->points.at(s - 1), wire->points.at(s))) {
                    onSegment = true;
                    break;
                }
            }
            if (!onSegment)
                continue;

            if (terminal.wire)
                uniteWires(terminal, wireTouch);
            else
                uniteTerminalWith(terminal, wireTouch);
        }
    }
}

void NetlistBuilder::applyLabels()
{
    // Une etiquette de folio ne fusionne que dans son folio ; un renvoi de
    // folio fusionne a travers tout le projet. C'est ce qui rend la continuite
    // electrique inter-folios reelle et non simplement dessinee.
    QHash<QString, int> firstOfKey;
    for (const LabelRecord &label : m_labels) {
        if (label.name.isEmpty())
            continue;
        const QString key = label.scope == Label::Scope::Project
                ? QStringLiteral("*/") + label.name
                : label.folioId + QLatin1Char('/') + label.name;
        auto it = firstOfKey.find(key);
        if (it == firstOfKey.end())
            firstOfKey.insert(key, label.node);
        else
            m_sets.unite(it.value(), label.node);
    }
}

void NetlistBuilder::assemble(Netlist &out)
{
    QHash<int, int> rootToNet;
    auto netFor = [&](int node) -> Netlist::Net & {
        const int root = m_sets.find(node);
        auto it = rootToNet.find(root);
        if (it == rootToNet.end()) {
            Netlist::Net net;
            net.id = int(out.m_nets.size());
            out.m_nets.append(net);
            it = rootToNet.insert(root, net.id);
        }
        return out.m_nets[it.value()];
    };

    // Fils : une entree par conducteur.
    QSet<QString> seenWireKeys;
    for (int i = 0; i < int(m_nodes.size()); ++i) {
        const NodeInfo &info = m_nodes.at(i);
        if (info.wireId.isEmpty())
            continue;
        const QString key = Netlist::wireKey(info.wireId, info.conductor);
        if (seenWireKeys.contains(key))
            continue;
        seenWireKeys.insert(key);

        Netlist::Net &net = netFor(i);
        Netlist::WireRef ref;
        ref.folioId = info.folioId;
        ref.wireId = info.wireId;
        ref.conductor = info.conductor;
        ref.conductorName = info.conductorName;
        net.wires.append(ref);
        out.m_wireNet.insert(key, net.id);
        if (!net.folioIds.contains(info.folioId))
            net.folioIds.append(info.folioId);
    }

    // Broches.
    for (const PinRecord &pin : m_pins) {
        Netlist::Net &net = netFor(pin.node);
        net.pins.append(pin.ref);
        net.points.append(pin.ref.position);
        out.m_pinNet.insert(Netlist::pinKey(pin.ref.symbolId, pin.ref.pinNumber), net.id);
        if (!net.folioIds.contains(pin.ref.folioId))
            net.folioIds.append(pin.ref.folioId);
    }

    // Etiquettes : elles nomment le potentiel.
    for (const LabelRecord &label : m_labels) {
        if (label.name.isEmpty())
            continue;
        Netlist::Net &net = netFor(label.node);
        if (!net.labels.contains(label.name))
            net.labels.append(label.name);
        if (!net.folioIds.contains(label.folioId))
            net.folioIds.append(label.folioId);
    }

    for (Netlist::Net &net : out.m_nets) {
        std::sort(net.labels.begin(), net.labels.end());
        std::sort(net.folioIds.begin(), net.folioIds.end());
        if (!net.labels.isEmpty())
            net.name = net.labels.first();
    }

    // Un potentiel a un seul point de connexion est presque toujours un fil
    // reste en l'air.
    for (const Netlist::Net &net : out.m_nets) {
        const int connections = net.pinCount() + (net.crossesFolios() ? 2 : 0)
                + (net.labels.isEmpty() ? 0 : 1);
        if (connections <= 1 && !net.wires.isEmpty()) {
            Netlist::Diagnostic d;
            d.severity = Netlist::Diagnostic::Severity::Warning;
            d.code = QStringLiteral("net.dangling");
            d.message = QStringLiteral("Potentiel sans point de connexion utile");
            d.folioId = net.wires.first().folioId;
            d.entityId = net.wires.first().wireId;
            m_diagnostics.append(d);
        }
    }

    out.m_diagnostics = m_diagnostics;
}

Netlist NetlistBuilder::build()
{
    for (const Folio *folio : m_project.folios())
        collectFolio(*folio);
    connectAtSites();
    connectTaps();
    applyLabels();

    Netlist result;
    assemble(result);
    return result;
}

// --------------------------------------------------------------------------

Netlist Netlist::build(const Project &project)
{
    NetlistBuilder builder(project);
    return builder.build();
}

QString Netlist::wireKey(const QString &wireId, int conductor)
{
    return wireId + QLatin1Char('#') + QString::number(conductor);
}

QString Netlist::pinKey(const QString &symbolId, const QString &pinNumber)
{
    return symbolId + QLatin1Char('#') + pinNumber;
}

const Netlist::Net *Netlist::net(int id) const
{
    if (id < 0 || id >= int(m_nets.size()))
        return nullptr;
    return &m_nets.at(id);
}

const Netlist::Net *Netlist::netOfWire(const QString &wireId, int conductor) const
{
    auto it = m_wireNet.constFind(wireKey(wireId, conductor));
    return it == m_wireNet.constEnd() ? nullptr : net(it.value());
}

const Netlist::Net *Netlist::netOfPin(const QString &symbolId, const QString &pinNumber) const
{
    auto it = m_pinNet.constFind(pinKey(symbolId, pinNumber));
    return it == m_pinNet.constEnd() ? nullptr : net(it.value());
}

QVector<const Netlist::Net *> Netlist::danglingNets() const
{
    QVector<const Net *> out;
    for (const Net &n : m_nets) {
        if (n.pinCount() <= 1 && n.labels.isEmpty())
            out.append(&n);
    }
    return out;
}

QVector<Netlist::PinRef> Netlist::unconnectedPins() const
{
    QVector<PinRef> out;
    for (const Net &n : m_nets) {
        if (n.pinCount() == 1 && n.wires.isEmpty() && n.labels.isEmpty())
            out.append(n.pins.first());
    }
    return out;
}

} // namespace dsn
