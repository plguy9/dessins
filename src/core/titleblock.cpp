#include "titleblock.h"

#include "folio.h"
#include "jsonutils.h"
#include "project.h"

#include <QJsonArray>

#include <algorithm>
#include <QJsonObject>

namespace dsn {

namespace {

QString kindTag(TitleBlockCell::Kind kind)
{
    switch (kind) {
    case TitleBlockCell::Kind::Text: return QStringLiteral("text");
    case TitleBlockCell::Kind::Image: return QStringLiteral("image");
    case TitleBlockCell::Kind::Table: return QStringLiteral("table");
    case TitleBlockCell::Kind::Field: break;
    }
    return QStringLiteral("field");
}

TitleBlockCell::Kind kindFromTag(const QString &tag)
{
    if (tag == QLatin1String("text"))
        return TitleBlockCell::Kind::Text;
    if (tag == QLatin1String("image"))
        return TitleBlockCell::Kind::Image;
    if (tag == QLatin1String("table"))
        return TitleBlockCell::Kind::Table;
    return TitleBlockCell::Kind::Field;
}

// Raccourcis de construction. Un gabarit s'ecrit alors comme il se lit, ce qui
// compte : c'est le seul endroit du code ou l'on decrit un dessin case par
// case, et une liste d'affectations le rendrait illisible.
TitleBlockCell champ(const QRectF &r, const QString &label, const QString &key,
                     double textHeight = 2.4,
                     TitleBlockCell::Layout layout = TitleBlockCell::Layout::Inline)
{
    TitleBlockCell c;
    c.kind = TitleBlockCell::Kind::Field;
    c.rect = r;
    c.label = label;
    c.key = key;
    c.textHeight = textHeight;
    c.layout = layout;
    return c;
}

TitleBlockCell titre(const QRectF &r, const QString &text, double height = 2.0)
{
    TitleBlockCell c;
    c.kind = TitleBlockCell::Kind::Text;
    c.rect = r;
    c.text = text;
    c.textHeight = height;
    c.align = Primitive::Align::Center;
    return c;
}

TitleBlockCell table(const QRectF &r, const QString &key, const QStringList &columns,
                     const QVector<double> &widths)
{
    TitleBlockCell c;
    c.kind = TitleBlockCell::Kind::Table;
    c.rect = r;
    c.key = key;
    c.columns = columns;
    c.widths = widths;
    c.textHeight = 1.8;
    c.align = Primitive::Align::Center;
    return c;
}

TitleBlockCell image(const QRectF &r, const QString &key)
{
    TitleBlockCell c;
    c.kind = TitleBlockCell::Kind::Image;
    c.rect = r;
    c.key = key;
    c.border = false;
    return c;
}

} // namespace

// --------------------------------------------------------------------------

QJsonObject TitleBlockCell::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("kind")] = kindTag(kind);
    o[QStringLiteral("rect")] = QJsonArray{ roundStorage(rect.x()), roundStorage(rect.y()),
                                            roundStorage(rect.width()),
                                            roundStorage(rect.height()) };
    if (!label.isEmpty())
        o[QStringLiteral("label")] = label;
    if (!key.isEmpty())
        o[QStringLiteral("key")] = key;
    if (!text.isEmpty())
        o[QStringLiteral("text")] = text;
    if (!columns.isEmpty())
        o[QStringLiteral("columns")] = stringListToJson(columns);
    if (!widths.isEmpty()) {
        QJsonArray a;
        for (double w : widths)
            a.append(roundStorage(w));
        o[QStringLiteral("widths")] = a;
    }
    o[QStringLiteral("labelHeight")] = roundStorage(labelHeight);
    o[QStringLiteral("textHeight")] = roundStorage(textHeight);
    if (layout == Layout::Stacked)
        o[QStringLiteral("layout")] = QStringLiteral("stacked");
    if (align != Primitive::Align::Left)
        o[QStringLiteral("align")] = Primitive::alignTag(align);
    if (!border)
        o[QStringLiteral("border")] = false;
    return o;
}

TitleBlockCell TitleBlockCell::fromJson(const QJsonValue &value)
{
    TitleBlockCell c;
    const QJsonObject o = value.toObject();
    c.kind = kindFromTag(o.value(QStringLiteral("kind")).toString());
    const QJsonArray r = o.value(QStringLiteral("rect")).toArray();
    if (r.size() == 4)
        c.rect = QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(),
                        r.at(3).toDouble());
    c.label = o.value(QStringLiteral("label")).toString();
    c.key = o.value(QStringLiteral("key")).toString();
    c.text = o.value(QStringLiteral("text")).toString();
    c.columns = stringListFromJson(o.value(QStringLiteral("columns")));
    for (const QJsonValue &w : o.value(QStringLiteral("widths")).toArray())
        c.widths.append(w.toDouble());
    c.labelHeight = o.value(QStringLiteral("labelHeight")).toDouble(1.6);
    c.textHeight = o.value(QStringLiteral("textHeight")).toDouble(2.5);
    c.layout = o.value(QStringLiteral("layout")).toString() == QLatin1String("stacked")
            ? Layout::Stacked
            : Layout::Inline;
    c.align = Primitive::alignFromTag(o.value(QStringLiteral("align")).toString());
    c.border = o.value(QStringLiteral("border")).toBool(true);
    return c;
}

QJsonObject TitleBlockTemplate::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("width")] = roundStorage(width);
    o[QStringLiteral("height")] = roundStorage(height);
    QJsonArray a;
    for (const TitleBlockCell &cell : cells)
        a.append(cell.toJson());
    o[QStringLiteral("cells")] = a;
    return o;
}

TitleBlockTemplate TitleBlockTemplate::fromJson(const QJsonValue &value)
{
    TitleBlockTemplate t;
    const QJsonObject o = value.toObject();
    t.id = o.value(QStringLiteral("id")).toString();
    t.name = o.value(QStringLiteral("name")).toString();
    t.width = o.value(QStringLiteral("width")).toDouble(180.0);
    t.height = o.value(QStringLiteral("height")).toDouble(40.0);
    for (const QJsonValue &c : o.value(QStringLiteral("cells")).toArray())
        t.cells.append(TitleBlockCell::fromJson(c));
    return t;
}

// --------------------------------------------------------------------------
// Les gabarits livres

TitleBlockTemplate TitleBlock::standard()
{
    // Le cartouche que le peintre dessinait en dur, transpose case par case :
    // memes bandes, memes champs, memes places. Le transposer plutot que d'en
    // inventer un autre a une raison — un dossier ouvert apres la mise a jour
    // doit se reconnaitre. Reserve honnete : les valeurs sont desormais
    // centrees dans leur case, ce qui les descend ou les monte d'un ou deux
    // millimetres par rapport au trace en dur.
    TitleBlockTemplate t;
    t.id = QStringLiteral("standard");
    t.name = QStringLiteral("Arcus — standard");
    t.width = 180.0;
    t.height = 40.0;

    const double h = t.height / 3.0;
    const double split = t.width * 0.62;

    t.cells.append(champ(QRectF(0, 0, t.width, h), {}, QStringLiteral("projectTitle"), 3.6));
    t.cells.append(champ(QRectF(0, h, split, h / 2.0), {}, QStringLiteral("folioTitle"), 2.6));
    t.cells.append(champ(QRectF(0, h + h / 2.0, split, h / 2.0), {},
                         QStringLiteral("client"), 2.2));
    t.cells.append(champ(QRectF(split, h, t.width - split, h / 2.0),
                         QStringLiteral("Folio"), QStringLiteral("folioNumber"), 2.6));
    t.cells.append(champ(QRectF(split, h + h / 2.0, t.width - split, h / 2.0),
                         QStringLiteral("Réf."), QStringLiteral("reference"), 2.2));
    t.cells.append(champ(QRectF(0, 2 * h, split, h / 2.0), {}, QStringLiteral("author"), 2.2));
    t.cells.append(champ(QRectF(0, 2 * h + h / 2.0, split, h / 2.0), {},
                         QStringLiteral("date"), 2.2));
    t.cells.append(champ(QRectF(split, 2 * h, t.width - split, h / 2.0),
                         QStringLiteral("Indice"), QStringLiteral("revision"), 2.2));
    t.cells.append(champ(QRectF(split, 2 * h + h / 2.0, t.width - split, h / 2.0), {},
                         QStringLiteral("sheetFormat"), 2.2));
    return t;
}

TitleBlockTemplate TitleBlock::loopSheet()
{
    // Calque sur les planches de schema de boucle relevees. Il ne sert pas
    // qu'a offrir un second choix : il PROUVE que le mecanisme suffit a
    // decrire un cartouche reel — tables qui grandissent, images, quatre
    // lignes de description. Si un gabarit reel n'y rentrait pas, ce serait
    // le mecanisme qu'il faudrait reprendre.
    TitleBlockTemplate t;
    t.id = QStringLiteral("boucle");
    t.name = QStringLiteral("Schéma de boucle");
    t.width = 330.0;
    t.height = 35.0;

    // Quatre colonnes de meme hauteur, comme sur les planches relevees : les
    // tables, l'approbation, les coordonnees, puis le pave d'identification.

    // 1. Les trois tables. Elles grandissent vers le HAUT : l'indice 0 se
    // pose juste au-dessus de l'intitule des colonnes, la revision 1 au-dessus
    // de lui. C'est l'ordre dans lequel on relit l'historique d'une planche.
    t.cells.append(table(QRectF(0, 0, 68, 26), QStringLiteral("routing"),
                         { QStringLiteral("PAR"), QStringLiteral("APP"), QStringLiteral("DATE"),
                           QStringLiteral("DESCRIPTION"), QStringLiteral("REV") },
                         { 1, 1, 1.6, 4, 1 }));
    t.cells.append(titre(QRectF(0, 26, 68, 5), QStringLiteral("CHEMINEMENT"), 2.4));
    t.cells.append(table(QRectF(68, 0, 56, 26), QStringLiteral("references"),
                         { QStringLiteral("NO DESSIN"), QStringLiteral("DESCRIPTION") },
                         { 2, 3 }));
    t.cells.append(titre(QRectF(68, 26, 56, 5), QStringLiteral("REFERENCES"), 2.4));
    t.cells.append(table(QRectF(124, 0, 86, 26), QStringLiteral("revisions"),
                         { QStringLiteral("NO"), QStringLiteral("ZONE"), QStringLiteral("DATE"),
                           QStringLiteral("DESCRIPTION"), QStringLiteral("PAR"),
                           QStringLiteral("VER"), QStringLiteral("APP") },
                         { 0.8, 1, 1.6, 4, 1, 1, 1 }));
    t.cells.append(titre(QRectF(124, 26, 86, 5), QStringLiteral("REVISIONS"), 2.4));

    // 2. L'approbation : le sceau d'ingenieur et les trois signatures.
    t.cells.append(titre(QRectF(210, 0, 30, 4.5), QStringLiteral("APPROBATION"), 1.8));
    t.cells.append(image(QRectF(211, 5, 28, 14), QStringLiteral("seal")));
    t.cells.append(champ(QRectF(210, 19.5, 30, 5), QStringLiteral("Demandé"),
                         QStringLiteral("requestedBy"), 1.8));
    t.cells.append(champ(QRectF(210, 24.5, 30, 5), QStringLiteral("Vérifié"),
                         QStringLiteral("checkedBy"), 1.8));
    t.cells.append(champ(QRectF(210, 29.5, 30, 5.5), QStringLiteral("Approuvé"),
                         QStringLiteral("approvedBy"), 1.8));

    // 3. Les coordonnees du dossier.
    t.cells.append(titre(QRectF(240, 0, 34, 4.5), QStringLiteral("COORDONNÉES"), 1.8));
    t.cells.append(champ(QRectF(240, 4.5, 34, 5), QStringLiteral("Secteur"),
                         QStringLiteral("sector"), 1.8));
    t.cells.append(champ(QRectF(240, 9.5, 34, 5), QStringLiteral("Projet"),
                         QStringLiteral("reference"), 1.8));
    t.cells.append(champ(QRectF(240, 14.5, 34, 5), QStringLiteral("Dossier"),
                         QStringLiteral("fileRef"), 1.8));
    t.cells.append(champ(QRectF(240, 19.5, 34, 5), QStringLiteral("Dessiné"),
                         QStringLiteral("author"), 1.8));
    t.cells.append(champ(QRectF(240, 24.5, 34, 5), QStringLiteral("Échelle"),
                         QStringLiteral("scale"), 1.8));
    t.cells.append(champ(QRectF(240, 29.5, 34, 5.5), QStringLiteral("Date"),
                         QStringLiteral("date"), 1.8));

    // 4. Le pave d'identification : quatre lignes de description — QUATRE, et
    // pas trois, c'est ce que portent les planches relevees (procede,
    // fonction, boucle, type de document) — puis le logo, puis le numero de
    // dessin et la revision, qui sont ce qu'on lit en premier de loin.
    // Un seul cadre autour des quatre lignes : ce sont quatre lignes d'un
    // meme paragraphe, pas quatre champs. Les separer d'un filet donnerait a
    // lire quatre renseignements independants.
    {
        TitleBlockCell cadre;
        cadre.kind = TitleBlockCell::Kind::Text;
        cadre.rect = QRectF(274, 0, 56, 14.6);
        t.cells.append(cadre);
    }
    for (int i = 0; i < 4; ++i) {
        TitleBlockCell ligne = champ(QRectF(274, 0.5 + i * 3.4, 56, 3.4), {},
                                     QStringLiteral("description%1").arg(i + 1), 2.0);
        ligne.border = false;
        t.cells.append(ligne);
    }
    t.cells.append(image(QRectF(276, 15, 52, 10), QStringLiteral("logo")));
    t.cells.append(champ(QRectF(274, 26, 42, 9), QStringLiteral("No dessin"),
                         QStringLiteral("drawingNumber"), 4.5,
                         TitleBlockCell::Layout::Stacked));
    t.cells.append(champ(QRectF(316, 26, 14, 9), QStringLiteral("Rév."),
                         QStringLiteral("revision"), 4.5, TitleBlockCell::Layout::Stacked));

    // La hauteur se deduit de ce que le gabarit contient reellement : la
    // poser a la main serait une seconde source de verite, et le cadre
    // reserverait trop ou trop peu.
    double bas = 0.0;
    for (const TitleBlockCell &cell : t.cells)
        bas = std::max(bas, cell.rect.bottom());
    t.height = bas;
    return t;
}

QVector<TitleBlockTemplate> TitleBlock::builtins()
{
    return { standard(), loopSheet() };
}

// --------------------------------------------------------------------------

QMap<QString, QString> TitleBlock::values(const Project &project, const Folio &folio)
{
    QMap<QString, QString> v;
    const ProjectInfo &info = project.info;

    v.insert(QStringLiteral("projectTitle"), info.title);
    v.insert(QStringLiteral("client"), info.client);
    v.insert(QStringLiteral("reference"), info.reference);
    v.insert(QStringLiteral("author"), info.author);
    v.insert(QStringLiteral("revision"), info.revision);
    v.insert(QStringLiteral("date"),
             info.date.isValid() ? info.date.toString(QStringLiteral("dd/MM/yyyy")) : QString());
    v.insert(QStringLiteral("notes"), info.notes);

    v.insert(QStringLiteral("folioNumber"), folio.number);
    v.insert(QStringLiteral("folioTitle"), folio.title);
    v.insert(QStringLiteral("sheetFormat"), folio.sheet.id);

    // Le rang du folio et le total : « 3 / 12 » est ce qu'un lecteur cherche
    // en premier dans une liasse, et aucun champ ne le portait.
    const int index = project.indexOf(folio.id());
    if (index >= 0) {
        v.insert(QStringLiteral("sheetIndex"), QString::number(index + 1));
        v.insert(QStringLiteral("sheetCount"), QString::number(project.folioCount()));
        v.insert(QStringLiteral("sheetOfTotal"),
                 QStringLiteral("%1 / %2").arg(index + 1).arg(project.folioCount()));
    }

    // Les champs du PROJET, puis ceux du FOLIO : le folio gagne, parce qu'un
    // champ pose sur une planche est plus precis que le reglage du dossier.
    for (auto it = info.extra.cbegin(); it != info.extra.cend(); ++it)
        v.insert(it.key(), it.value());
    for (auto it = folio.titleBlock.cbegin(); it != folio.titleBlock.cend(); ++it)
        v.insert(it.key(), it.value());
    return v;
}

QMap<QString, QString> TitleBlock::fieldCaptions()
{
    return {
        { QStringLiteral("projectTitle"), QStringLiteral("Titre du projet") },
        { QStringLiteral("client"), QStringLiteral("Client") },
        { QStringLiteral("reference"), QStringLiteral("Référence du projet") },
        { QStringLiteral("author"), QStringLiteral("Dessiné par") },
        { QStringLiteral("revision"), QStringLiteral("Indice de révision") },
        { QStringLiteral("date"), QStringLiteral("Date") },
        { QStringLiteral("notes"), QStringLiteral("Notes du projet") },
        { QStringLiteral("folioNumber"), QStringLiteral("Numéro du folio") },
        { QStringLiteral("folioTitle"), QStringLiteral("Titre du folio") },
        { QStringLiteral("sheetFormat"), QStringLiteral("Format de feuille") },
        { QStringLiteral("sheetIndex"), QStringLiteral("Rang de la feuille") },
        { QStringLiteral("sheetCount"), QStringLiteral("Nombre de feuilles") },
        { QStringLiteral("sheetOfTotal"), QStringLiteral("Feuille n / total") },
        { QStringLiteral("sector"), QStringLiteral("Secteur") },
        { QStringLiteral("fileRef"), QStringLiteral("Dossier") },
        { QStringLiteral("scale"), QStringLiteral("Échelle") },
        { QStringLiteral("drawingNumber"), QStringLiteral("Numéro de dessin") },
        { QStringLiteral("requestedBy"), QStringLiteral("Demandé par") },
        { QStringLiteral("checkedBy"), QStringLiteral("Vérifié par") },
        { QStringLiteral("approvedBy"), QStringLiteral("Approuvé par") },
        { QStringLiteral("description1"), QStringLiteral("Description 1") },
        { QStringLiteral("description2"), QStringLiteral("Description 2") },
        { QStringLiteral("description3"), QStringLiteral("Description 3") },
        { QStringLiteral("description4"), QStringLiteral("Description 4") },
    };
}

} // namespace dsn
