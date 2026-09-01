#include "surferdialog.h"

#include "core/entities.h"
#include "rules/crossref.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace dsn {

namespace {

QString describe(const Project &project, const Folio &folio, const Entity &entity)
{
    if (const auto *symbol = dynamic_cast<const SymbolInstance *>(&entity)) {
        const SymbolDefinition *definition = project.library.definition(symbol->definitionId);
        const QString name = definition ? definition->name : symbol->definitionId;
        return symbol->designation().isEmpty() ? name
                                               : symbol->designation() + QStringLiteral(" — ")
                        + name;
    }
    if (const auto *label = dynamic_cast<const Label *>(&entity)) {
        switch (label->role) {
        case Label::Role::Source:
            return QObject::tr("Signal « %1 » — source").arg(label->name);
        case Label::Role::Destination:
            return QObject::tr("Signal « %1 » — destination").arg(label->name);
        case Label::Role::Plain:
            return QObject::tr("Renvoi « %1 »").arg(label->name);
        }
    }
    if (const auto *wire = dynamic_cast<const Wire *>(&entity)) {
        return wire->number.isEmpty() ? QObject::tr("Fil") : QObject::tr("Fil %1").arg(wire->number);
    }
    Q_UNUSED(folio);
    return QObject::tr("Élément");
}

QString whereIs(const Folio &folio, const QPointF &point)
{
    const QString location = CrossReference::locationOf(folio, point);
    const QString title = folio.title.isEmpty() ? QString() : folio.title;
    if (location.isEmpty())
        return title;
    return title.isEmpty() ? location : location + QStringLiteral(" — ") + title;
}

QPointF anchorOf(const Entity &entity)
{
    if (const auto *symbol = dynamic_cast<const SymbolInstance *>(&entity))
        return symbol->placement.position;
    if (const auto *label = dynamic_cast<const Label *>(&entity))
        return label->point;
    return entity.boundingBox().center();
}

} // namespace

QVector<SurferDialog::Site> SurferDialog::sitesFor(const Project &project,
                                                   const Netlist &netlist,
                                                   const QString &entityId)
{
    QVector<Site> sites;
    const Folio *sourceFolio = nullptr;
    const Entity *entity = project.findEntity(entityId, &sourceFolio);
    if (!entity)
        return sites;

    auto add = [&](const Folio &folio, const Entity &target) {
        if (target.id() == entityId)
            return;
        for (const Site &existing : sites) {
            if (existing.entityId == target.id())
                return;
        }
        Site site;
        site.folioId = folio.id();
        site.entityId = target.id();
        site.title = describe(project, folio, target);
        site.detail = whereIs(folio, anchorOf(target));
        sites.append(site);
    };

    // Un appareil : ses autres blocs. La bobine et ses contacts sont le meme
    // appareil, poses a trois endroits du dossier.
    if (const auto *symbol = dynamic_cast<const SymbolInstance *>(entity)) {
        if (!symbol->deviceGroup.isEmpty()) {
            for (const Folio *folio : project.folios()) {
                for (const SymbolInstance *other : folio->entitiesOfType<SymbolInstance>()) {
                    if (other->deviceGroup == symbol->deviceGroup)
                        add(*folio, *other);
                }
            }
        }
        // Puis les potentiels qui le touchent, par leurs autres broches :
        // suivre un fil est l'autre moitie du geste.
        for (const Netlist::Net &net : netlist.nets()) {
            const bool touches = std::any_of(net.pins.cbegin(), net.pins.cend(),
                                             [&](const Netlist::PinRef &pin) {
                                                 return pin.symbolId == symbol->id();
                                             });
            if (!touches)
                continue;
            for (const Netlist::PinRef &pin : net.pins) {
                if (pin.symbolId == symbol->id())
                    continue;
                const Folio *folio = project.folio(pin.folioId);
                const Entity *target = folio ? folio->entity(pin.symbolId) : nullptr;
                if (folio && target)
                    add(*folio, *target);
            }
        }
    }

    // Une etiquette de portee projet : les autres bouts du meme nom de code.
    if (const auto *label = dynamic_cast<const Label *>(entity)) {
        if (label->scope == Label::Scope::Project) {
            for (const Folio *folio : project.folios()) {
                for (const Label *other : folio->entitiesOfType<Label>()) {
                    if (other->name == label->name && other->scope == Label::Scope::Project)
                        add(*folio, *other);
                }
            }
        }
    }

    return sites;
}

SurferDialog::SurferDialog(Document *document, const QString &entityId, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setWindowTitle(tr("Surfer — références croisées"));
    resize(560, 380);

    m_sites = sitesFor(document->project(), document->netlist(), entityId);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(m_sites.isEmpty()
                                    ? tr("Rien de lié à cet élément dans le dossier.")
                                    : tr("Double-cliquez une destination pour y aller."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    for (const Site &site : std::as_const(m_sites)) {
        auto *item = new QListWidgetItem(site.detail.isEmpty()
                                                 ? site.title
                                                 : site.title + QStringLiteral("\n") + site.detail);
        m_list->addItem(item);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    layout->addWidget(m_list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Y aller"));
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_sites.isEmpty());
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this] { jumpTo(m_list->currentRow()); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) { jumpTo(m_list->row(item)); });
}

void SurferDialog::jumpTo(int row)
{
    if (row < 0 || row >= m_sites.size())
        return;
    const Site &site = m_sites.at(row);
    Q_EMIT locateRequested(site.folioId, site.entityId);
    accept();
}

} // namespace dsn
