#include "findreplace.h"

#include "core/entities.h"

#include <QRegularExpression>

namespace dsn {

namespace {

Qt::CaseSensitivity casse(const FindQuery &query)
{
    return query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
}

// « Mot entier » passe par une expression reguliere parce que c'est la seule
// facon d'exprimer une frontiere de mot sans se tromper sur les accents et la
// ponctuation. Le motif est echappe : on cherche du texte, pas une regex —
// un repere qui contient un point ne doit pas se mettre a tout attraper.
QRegularExpression wordPattern(const FindQuery &query)
{
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (!query.caseSensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(QStringLiteral("\\b%1\\b").arg(
                                      QRegularExpression::escape(query.needle)),
                              options);
}

} // namespace

bool FindReplace::matches(const QString &source, const FindQuery &query)
{
    if (query.needle.isEmpty() || source.isEmpty())
        return false;
    if (query.wholeWord)
        return wordPattern(query).match(source).hasMatch();
    return source.contains(query.needle, casse(query));
}

QString FindReplace::replaced(const QString &source, const FindQuery &query)
{
    if (query.needle.isEmpty())
        return source;
    if (query.wholeWord) {
        QString out = source;
        return out.replace(wordPattern(query), query.replacement);
    }
    QString out = source;
    return out.replace(query.needle, query.replacement, casse(query));
}

QVector<FindHit> FindReplace::find(const Project &project, const FindQuery &query)
{
    QVector<FindHit> hits;
    // Chercher une chaine vide renverrait tout le dossier : ce n'est pas une
    // recherche, c'est un inventaire, et personne ne l'a demande.
    if (query.needle.isEmpty())
        return hits;

    for (int index = 0; index < project.folioCount(); ++index) {
        const Folio *folio = project.folioAt(index);
        if (!folio || !query.scope.includes(folio->id()))
            continue;

        const QString label = folio->title.isEmpty()
                ? folio->number
                : QStringLiteral("%1 — %2").arg(folio->number, folio->title);

        auto ajouter = [&](const Entity &entity, const QString &where, const QString &field,
                           const QString &before) {
            if (!matches(before, query))
                return;
            FindHit hit;
            hit.folioId = folio->id();
            hit.folioLabel = label;
            hit.entityId = entity.id();
            // Le lieu, comme dans l'audit : sans la zone, retrouver une
            // occurrence sur une planche A3 dense revient a la chercher a
            // l'oeil.
            hit.zone = folio->zoneAt(entity.boundingBox().center());
            hit.where = where;
            hit.field = field;
            hit.before = before;
            hit.after = replaced(before, query);
            hits.append(hit);
        };

        for (const EntityPtr &held : folio->entities()) {
            const Entity *entity = held.get();
            if (const auto *text = dynamic_cast<const TextItem *>(entity)) {
                if (query.inTexts)
                    ajouter(*text, QStringLiteral("Texte"), {}, text->text);
                continue;
            }
            if (const auto *label2 = dynamic_cast<const Label *>(entity)) {
                if (query.inLabels)
                    ajouter(*label2, QStringLiteral("Etiquette"), {}, label2->name);
                continue;
            }
            if (const auto *wire = dynamic_cast<const Wire *>(entity)) {
                if (query.inWireNumbers)
                    ajouter(*wire, QStringLiteral("Repere de fil"), {}, wire->number);
                continue;
            }
            if (const auto *symbol = dynamic_cast<const SymbolInstance *>(entity)) {
                for (auto it = symbol->fields.cbegin(); it != symbol->fields.cend(); ++it) {
                    const bool repere = it.key() == QStringLiteral("designation");
                    if (repere ? !query.inDesignations : !query.inFields)
                        continue;
                    ajouter(*symbol,
                            repere ? QStringLiteral("Repere")
                                   : QStringLiteral("Champ %1").arg(it.key()),
                            it.key(), it.value());
                }
                continue;
            }
        }
    }
    return hits;
}

} // namespace dsn
