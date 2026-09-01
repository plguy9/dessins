#include "crossref.h"

#include "core/entities.h"

#include <algorithm>

namespace dsn {

namespace {

struct Site {
    QString entityId;
    QString folioId;
    QString location;   // « 2/A3 »
    Label::Role role = Label::Role::Plain;
    int folioIndex = 0;
};

} // namespace

QString CrossReference::locationOf(const Folio &folio, const QPointF &point)
{
    const QString zone = folio.zoneAt(point);
    const QString tag = folio.number.isEmpty() ? folio.title : folio.number;
    if (tag.isEmpty())
        return zone;
    return zone.isEmpty() ? tag : tag + QLatin1Char('/') + zone;
}

QHash<QString, QString> CrossReference::resolve(const Project &project, const Netlist &netlist)
{
    Q_UNUSED(netlist);

    // Les renvois se regroupent par nom de code, pas par potentiel : c'est le
    // nom qui relie les deux bouts, et il doit se lire meme si le trace n'est
    // pas encore fait.
    QHash<QString, QVector<Site>> byName;
    QHash<QString, QString> siteOfLabel;   // etiquette -> nom de code

    const auto folios = project.folios();
    for (int index = 0; index < int(folios.size()); ++index) {
        const Folio *folio = folios[std::size_t(index)];
        for (const Label *label : folio->entitiesOfType<Label>()) {
            if (label->name.isEmpty() || label->scope != Label::Scope::Project)
                continue;
            Site site;
            site.entityId = label->id();
            site.folioId = folio->id();
            site.location = locationOf(*folio, label->point);
            site.role = label->role;
            site.folioIndex = index;
            byName[label->name].append(site);
            siteOfLabel.insert(label->id(), label->name);
        }
    }

    QHash<QString, QString> out;
    for (int index = 0; index < int(folios.size()); ++index) {
        const Folio *folio = folios[std::size_t(index)];
        for (const Label *label : folio->entitiesOfType<Label>()) {
            const auto name = siteOfLabel.constFind(label->id());
            if (name == siteOfLabel.constEnd())
                continue;

            QStringList targets;
            for (const Site &site : byName.value(name.value())) {
                // On ne se renvoie pas a soi-meme, ni a une autre fleche du
                // meme role : une source pointe vers ses destinations.
                if (site.entityId == label->id())
                    continue;
                if (label->role != Label::Role::Plain && site.role == label->role)
                    continue;
                if (site.location.isEmpty() || targets.contains(site.location))
                    continue;
                targets.append(site.location);
            }
            if (targets.isEmpty())
                continue;
            targets.sort();

            // La fleche dit le sens : une source envoie, une destination
            // recoit. Un renvoi simple ne dit rien du sens, il enumere.
            const QString prefix = label->role == Label::Role::Source
                    ? QStringLiteral("→ ")
                    : label->role == Label::Role::Destination ? QStringLiteral("← ")
                                                              : QString();
            out.insert(label->id(), prefix + targets.join(QStringLiteral(", ")));
        }
    }
    return out;
}

} // namespace dsn
