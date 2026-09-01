#include "ladder.h"

#include "core/entities.h"

namespace dsn {

std::vector<EntityPtr> LadderBuilder::build(const LadderSpec &spec)
{
    std::vector<EntityPtr> out;
    if (spec.rungs < 1 || spec.width <= 0.0)
        return out;

    const double height = spec.height();

    // ---- les deux rails --------------------------------------------------
    auto makeRail = [&](const QPointF &top, const QString &name, const QString &type) {
        auto rail = std::make_unique<Wire>();
        rail->points = { top, top + QPointF(0.0, height) };
        rail->wireType = type;
        // Le nom du rail devient le repere du potentiel : c'est lui qui
        // gouverne tout le rail, et il est verrouille pour que le reperage
        // automatique ne le remplace pas par un numero de colonne.
        rail->number = name;
        rail->numberLocked = true;
        out.push_back(std::move(rail));

        auto label = std::make_unique<Label>();
        label->point = top;
        label->name = name;
        label->direction = Direction::Up;
        // Portee projet : un rail d'alimentation traverse tout le dossier.
        label->scope = Label::Scope::Project;
        out.push_back(std::move(label));
    };

    makeRail(spec.origin, spec.leftRailName, spec.leftRailType);
    makeRail(spec.rightTop(), spec.rightRailName, spec.rightRailType);

    // ---- barreaux et numeros de ligne ------------------------------------
    for (int i = 0; i < spec.rungs; ++i) {
        const QPointF left = spec.rungLeft(i);

        if (spec.drawRungs) {
            auto rung = std::make_unique<Wire>();
            rung->points = { left, left + QPointF(spec.width, 0.0) };
            rung->wireType = spec.rungType;
            out.push_back(std::move(rung));
        }

        if (spec.numberRungs) {
            auto number = std::make_unique<TextItem>();
            number->text = QString::number(spec.rungNumber(i));
            // Le numero se pose a gauche du rail, hors du circuit : il doit
            // se lire sans jamais etre pris pour un element du schema.
            number->placement.position = left + QPointF(-4.0, spec.numberHeight * 0.4);
            number->height = spec.numberHeight;
            number->align = TextItem::Align::Right;
            out.push_back(std::move(number));
        }
    }

    return out;
}

QString LadderBuilder::fitWarning(const LadderSpec &spec, const QRectF &frameRect)
{
    if (frameRect.isNull())
        return QString();

    const QRectF used(spec.origin.x() - 6.0, spec.origin.y(), spec.width + 6.0, spec.height());
    if (frameRect.contains(used))
        return QString();

    // Mieux vaut prevenir avant de poser cinquante entites hors cadre que de
    // laisser l'utilisateur decouvrir le debordement a l'impression.
    if (used.bottom() > frameRect.bottom()) {
        const int fits = int((frameRect.bottom() - spec.origin.y()) / spec.rungSpacing) + 1;
        return QObject::tr("L'échelle dépasse le bas du cadre : %1 lignes tiennent, %2 demandées.")
                .arg(std::max(0, fits))
                .arg(spec.rungs);
    }
    if (used.right() > frameRect.right())
        return QObject::tr("L'échelle dépasse le bord droit du cadre.");
    return QObject::tr("L'échelle sort du cadre du folio.");
}

} // namespace dsn
