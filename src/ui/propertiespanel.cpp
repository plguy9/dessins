#include "propertiespanel.h"

#include "core/documentcommands.h"
#include "core/wiretype.h"
#include "render/foliopainter.h"
#include "rules/crossref.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dsn {

namespace {

QLabel *readOnly(const QString &value, QWidget *parent)
{
    auto *label = new QLabel(value, parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QDoubleSpinBox *lengthBox(double value, QWidget *parent, double minimum = -10000.0)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setRange(minimum, 10000.0);
    box->setDecimals(2);
    box->setSingleStep(2.5);
    box->setSuffix(QStringLiteral(" mm"));
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

} // namespace

PropertiesPanel::PropertiesPanel(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QLabel(this);
    m_header->setContentsMargins(8, 6, 8, 6);
    QFont headerFont = m_header->font();
    headerFont.setBold(true);
    m_header->setFont(headerFont);
    layout->addWidget(m_header);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_scroll, 1);

    connect(m_document, &Document::changed, this, [this] {
        // Une modification venue du canevas doit se voir ici, mais rebatir le
        // formulaire pendant qu'on tape dedans ferait perdre le curseur.
        if (!m_rebuilding)
            rebuild();
    });
    connect(m_document, &Document::currentFolioChanged, this, [this] { rebuild(); });

    rebuild();
}

void PropertiesPanel::setSelection(const QSet<QString> &ids)
{
    m_selection = ids;
    rebuild();
}

template <typename T, typename Mutator>
void PropertiesPanel::modify(T *entity, const QString &text, Mutator mutate, int mergeId)
{
    Folio *folio = m_document->currentFolio();
    if (!folio || !entity)
        return;

    auto before = entity->clone();
    auto after = entity->clone();
    mutate(static_cast<T *>(after.get()));

    auto command = std::make_unique<ModifyEntityCommand>(m_document->project(), folio->id(),
                                                         std::move(before), std::move(after), text);
    command->setMergeId(mergeId);

    m_rebuilding = true;
    m_document->push(std::move(command));
    m_rebuilding = false;
}

void PropertiesPanel::rebuild()
{
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    form->setContentsMargins(8, 8, 8, 8);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    Folio *folio = m_document->currentFolio();

    if (!folio) {
        m_header->setText(tr("Aucun folio"));
    } else if (m_selection.isEmpty()) {
        m_header->setText(tr("Folio %1").arg(folio->number));
        buildFolioForm(form);
    } else if (m_selection.size() > 1) {
        m_header->setText(tr("%n élément(s) sélectionné(s)", "", int(m_selection.size())));
        buildMultiForm(form);
    } else {
        Entity *entity = folio->entity(*m_selection.cbegin());
        if (!entity) {
            m_header->setText(tr("Élément introuvable"));
        } else if (auto *symbol = dynamic_cast<SymbolInstance *>(entity)) {
            m_header->setText(tr("Appareil"));
            buildSymbolForm(form, symbol);
        } else if (auto *wire = dynamic_cast<Wire *>(entity)) {
            m_header->setText(tr("Fil"));
            buildWireForm(form, wire);
        } else if (auto *text = dynamic_cast<TextItem *>(entity)) {
            m_header->setText(tr("Texte"));
            buildTextForm(form, text);
        } else if (auto *label = dynamic_cast<Label *>(entity)) {
            m_header->setText(label->scope == Label::Scope::Project ? tr("Renvoi de folio")
                                                                    : tr("Étiquette de potentiel"));
            buildLabelForm(form, label);
        } else if (auto *junction = dynamic_cast<Junction *>(entity)) {
            m_header->setText(tr("Jonction"));
            buildJunctionForm(form, junction);
        } else if (auto *dimension = dynamic_cast<DimensionItem *>(entity)) {
            m_header->setText(tr("Cotation"));
            buildDimensionForm(form, dimension);
        }
    }

    m_scroll->setWidget(content);
}

void PropertiesPanel::buildFolioForm(QFormLayout *form)
{
    Folio *folio = m_document->currentFolio();
    QWidget *parent = form->parentWidget();

    auto *number = new QLineEdit(folio->number, parent);
    form->addRow(tr("Numéro"), number);
    connect(number, &QLineEdit::editingFinished, this, [this, number] {
        if (Folio *f = m_document->currentFolio()) {
            f->number = number->text();
            m_document->invalidateNetlist();
            Q_EMIT statusMessage(tr("Numéro de folio modifié"));
        }
    });

    auto *title = new QLineEdit(folio->title, parent);
    form->addRow(tr("Titre"), title);
    connect(title, &QLineEdit::editingFinished, this, [this, title] {
        if (Folio *f = m_document->currentFolio())
            f->title = title->text();
    });

    auto *format = new QComboBox(parent);
    const auto formats = allSheetFormats();
    for (const SheetFormat &f : formats)
        format->addItem(f.label, f.id);
    format->setCurrentIndex(std::max(0, format->findData(folio->sheet.id)));
    form->addRow(tr("Format"), format);
    connect(format, &QComboBox::currentIndexChanged, this, [this, format] {
        if (Folio *f = m_document->currentFolio()) {
            f->sheet = sheetFormatById(format->currentData().toString());
            Q_EMIT statusMessage(tr("Format de feuille : %1").arg(f->sheet.label));
        }
    });

    auto *columns = new QSpinBox(parent);
    columns->setRange(1, 40);
    columns->setValue(folio->frame.columns);
    form->addRow(tr("Colonnes"), columns);
    connect(columns, &QSpinBox::valueChanged, this, [this](int value) {
        if (Folio *f = m_document->currentFolio())
            f->frame.columns = value;
    });

    auto *rows = new QSpinBox(parent);
    rows->setRange(1, 26);
    rows->setValue(folio->frame.rows);
    form->addRow(tr("Lignes"), rows);
    connect(rows, &QSpinBox::valueChanged, this, [this](int value) {
        if (Folio *f = m_document->currentFolio())
            f->frame.rows = value;
    });

    form->addRow(tr("Éléments"), readOnly(QString::number(folio->entityCount()), parent));
}

void PropertiesPanel::buildSymbolForm(QFormLayout *form, SymbolInstance *symbol)
{
    QWidget *parent = form->parentWidget();
    const SymbolDefinition *definition =
            m_document->project().library.definition(symbol->definitionId);

    form->addRow(tr("Symbole"),
                 readOnly(definition ? definition->name : symbol->definitionId, parent));
    if (definition)
        form->addRow(tr("Identifiant"), readOnly(definition->id, parent));

    auto *designation = new QLineEdit(symbol->designation(), parent);
    form->addRow(tr("Désignation"), designation);
    connect(designation, &QLineEdit::editingFinished, this, [this, symbol, designation] {
        if (symbol->designation() == designation->text())
            return;
        modify(symbol, tr("Modifier la désignation"), [&](SymbolInstance *s) {
            s->setDesignation(designation->text());
            // Une designation saisie a la main se verrouille d'elle-meme :
            // c'est ce que l'utilisateur veut dire en la tapant.
            s->designationLocked = !designation->text().isEmpty();
        });
    });

    auto *locked = new QCheckBox(tr("Ne pas régénérer automatiquement"), parent);
    locked->setChecked(symbol->designationLocked);
    form->addRow(QString(), locked);
    connect(locked, &QCheckBox::toggled, this, [this, symbol](bool on) {
        modify(symbol, tr("Verrouiller la désignation"),
               [on](SymbolInstance *s) { s->designationLocked = on; });
    });

    struct FieldSpec {
        const char *key;
        const char *label;
    };
    static const FieldSpec fields[] = {
        { "value", QT_TR_NOOP("Valeur") },
        { "manufacturer", QT_TR_NOOP("Fabricant") },
        { "partNumber", QT_TR_NOOP("Référence") },
        { "function", QT_TR_NOOP("Fonction") },
        { "location", QT_TR_NOOP("Localisation") },
        { "terminal", QT_TR_NOOP("Borne") },
        { "comment", QT_TR_NOOP("Commentaire") },
    };
    for (const FieldSpec &spec : fields) {
        const QString key = QString::fromLatin1(spec.key);
        auto *edit = new QLineEdit(symbol->fields.value(key), parent);
        form->addRow(tr(spec.label), edit);
        connect(edit, &QLineEdit::editingFinished, this, [this, symbol, key, edit] {
            if (symbol->fields.value(key) == edit->text())
                return;
            modify(symbol, tr("Modifier une caractéristique"),
                   [&](SymbolInstance *s) { s->fields.insert(key, edit->text()); });
        });
    }

    auto *group = new QLineEdit(symbol->deviceGroup, parent);
    group->setPlaceholderText(tr("ex. KM1 — blocs d'un même appareil"));
    form->addRow(tr("Appareil"), group);
    connect(group, &QLineEdit::editingFinished, this, [this, symbol, group] {
        if (symbol->deviceGroup == group->text())
            return;
        modify(symbol, tr("Regrouper les blocs"),
               [&](SymbolInstance *s) { s->deviceGroup = group->text(); });
    });

    auto *orientation = new QComboBox(parent);
    orientation->addItem(QStringLiteral("0°"), 0);
    orientation->addItem(QStringLiteral("90°"), 90);
    orientation->addItem(QStringLiteral("180°"), 180);
    orientation->addItem(QStringLiteral("270°"), 270);
    orientation->setCurrentIndex(
            std::max(0, orientation->findData(toDegrees(symbol->placement.orientation))));
    form->addRow(tr("Rotation"), orientation);
    connect(orientation, &QComboBox::currentIndexChanged, this, [this, symbol, orientation] {
        const auto value = orientationFromDegrees(orientation->currentData().toInt());
        if (symbol->placement.orientation == value)
            return;
        modify(symbol, tr("Pivoter"),
               [value](SymbolInstance *s) { s->placement.orientation = value; });
    });

    auto *mirrored = new QCheckBox(tr("Miroir"), parent);
    mirrored->setChecked(symbol->placement.mirrored);
    form->addRow(QString(), mirrored);
    connect(mirrored, &QCheckBox::toggled, this, [this, symbol](bool on) {
        modify(symbol, tr("Retourner"), [on](SymbolInstance *s) { s->placement.mirrored = on; });
    });

    auto *x = lengthBox(symbol->placement.position.x(), parent);
    auto *y = lengthBox(symbol->placement.position.y(), parent);
    form->addRow(tr("X"), x);
    form->addRow(tr("Y"), y);
    connect(x, &QDoubleSpinBox::valueChanged, this, [this, symbol](double value) {
        modify(symbol, tr("Déplacer"),
               [value](SymbolInstance *s) { s->placement.position.setX(value); }, MergeMove);
    });
    connect(y, &QDoubleSpinBox::valueChanged, this, [this, symbol](double value) {
        modify(symbol, tr("Déplacer"),
               [value](SymbolInstance *s) { s->placement.position.setY(value); }, MergeMove);
    });

    // Potentiels raccordes : c'est la reponse a « ou va cette broche ».
    if (definition) {
        const Netlist &netlist = m_document->netlist();
        QStringList connections;
        for (const Pin &pin : definition->pins) {
            const Netlist::Net *net = netlist.netOfPin(symbol->id(), pin.number);
            QString potential = tr("libre");
            if (net) {
                potential = net->number.isEmpty()
                        ? (net->name.isEmpty() ? tr("potentiel %1").arg(net->id) : net->name)
                        : net->number;
            }
            connections.append(QStringLiteral("%1 → %2").arg(pin.number, potential));
        }
        form->addRow(tr("Raccordements"),
                     readOnly(connections.join(QStringLiteral("\n")), parent));
    }
}

void PropertiesPanel::buildWireForm(QFormLayout *form, Wire *wire)
{
    QWidget *parent = form->parentWidget();

    auto *number = new QLineEdit(wire->number, parent);
    form->addRow(tr("Repère"), number);
    connect(number, &QLineEdit::editingFinished, this, [this, wire, number] {
        if (wire->number == number->text())
            return;
        modify(wire, tr("Modifier le repère"), [&](Wire *w) {
            w->number = number->text();
            w->numberLocked = !number->text().isEmpty();
        });
    });

    auto *locked = new QCheckBox(tr("Ne pas régénérer automatiquement"), parent);
    locked->setChecked(wire->numberLocked);
    form->addRow(QString(), locked);
    connect(locked, &QCheckBox::toggled, this, [this, wire](bool on) {
        modify(wire, tr("Verrouiller le repère"), [on](Wire *w) { w->numberLocked = on; });
    });

    // Type de fil : c'est lui qui porte la couleur, la section et le calque.
    // Le choix se fait ici, pas fil par fil couleur par couleur.
    auto *type = new QComboBox(parent);
    for (const WireType &t : m_document->project().wireTypes.all()) {
        QPixmap swatch(14, 14);
        swatch.fill(FolioPainter::wireTypeColor(t));
        QString label = t.name.isEmpty() ? t.id : t.name;
        if (!t.crossSection.isEmpty())
            label += QStringLiteral(" — %1").arg(t.crossSection);
        type->addItem(QIcon(swatch), label, t.id);
    }
    const int typeIndex = type->findData(wire->wireType.isEmpty() ? WireTypeSet::defaultId()
                                                                 : wire->wireType);
    type->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
    form->addRow(tr("Type de fil"), type);
    connect(type, &QComboBox::currentIndexChanged, this, [this, wire, type](int index) {
        const QString id = type->itemData(index).toString();
        if (wire->wireType == id)
            return;
        modify(wire, tr("Changer le type de fil"), [&](Wire *w) { w->wireType = id; });
    });

    auto *conductors = new QLineEdit(wire->conductors.join(QStringLiteral(", ")), parent);
    conductors->setPlaceholderText(tr("vide = un conducteur ; ex. L1, L2, L3, N, PE"));
    form->addRow(tr("Conducteurs"), conductors);
    connect(conductors, &QLineEdit::editingFinished, this, [this, wire, conductors] {
        QStringList list = conductors->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString &item : list)
            item = item.trimmed();
        if (wire->conductors == list)
            return;
        modify(wire, tr("Modifier les conducteurs"), [&](Wire *w) { w->conductors = list; });
    });

    form->addRow(tr("Longueur"),
                 readOnly(QStringLiteral("%1 mm").arg(wire->length(), 0, 'f', 1), parent));
    form->addRow(tr("Sommets"), readOnly(QString::number(wire->points.size()), parent));

    const Netlist::Net *net = m_document->netlist().netOfWire(wire->id());
    if (net) {
        form->addRow(tr("Potentiel"),
                     readOnly(net->name.isEmpty() ? tr("sans nom") : net->name, parent));
        form->addRow(tr("Broches reliées"), readOnly(QString::number(net->pinCount()), parent));
        if (net->crossesFolios())
            form->addRow(tr("Folios"),
                         readOnly(QString::number(net->folioIds.size()), parent));
    }
}

void PropertiesPanel::buildTextForm(QFormLayout *form, TextItem *text)
{
    QWidget *parent = form->parentWidget();

    auto *content = new QLineEdit(text->text, parent);
    form->addRow(tr("Contenu"), content);
    connect(content, &QLineEdit::editingFinished, this, [this, text, content] {
        if (text->text == content->text())
            return;
        modify(text, tr("Modifier un texte"), [&](TextItem *t) { t->text = content->text(); });
    });

    auto *height = lengthBox(text->height, parent, 0.5);
    form->addRow(tr("Hauteur"), height);
    connect(height, &QDoubleSpinBox::valueChanged, this, [this, text](double value) {
        modify(text, tr("Modifier la hauteur"), [value](TextItem *t) { t->height = value; });
    });

    auto *align = new QComboBox(parent);
    align->addItem(tr("Gauche"), int(TextItem::Align::Left));
    align->addItem(tr("Centré"), int(TextItem::Align::Center));
    align->addItem(tr("Droite"), int(TextItem::Align::Right));
    align->setCurrentIndex(int(text->align));
    form->addRow(tr("Alignement"), align);
    connect(align, &QComboBox::currentIndexChanged, this, [this, text](int index) {
        modify(text, tr("Modifier l'alignement"),
               [index](TextItem *t) { t->align = TextItem::Align(index); });
    });
}

void PropertiesPanel::buildLabelForm(QFormLayout *form, Label *label)
{
    QWidget *parent = form->parentWidget();

    auto *name = new QLineEdit(label->name, parent);
    form->addRow(tr("Nom"), name);
    connect(name, &QLineEdit::editingFinished, this, [this, label, name] {
        if (label->name == name->text())
            return;
        modify(label, tr("Renommer le potentiel"), [&](Label *l) { l->name = name->text(); });
    });

    auto *scope = new QComboBox(parent);
    scope->addItem(tr("Folio — ne relie que cette page"), int(Label::Scope::Folio));
    scope->addItem(tr("Projet — renvoi de folio"), int(Label::Scope::Project));
    scope->setCurrentIndex(int(label->scope));
    form->addRow(tr("Portée"), scope);
    connect(scope, &QComboBox::currentIndexChanged, this, [this, label](int index) {
        modify(label, tr("Modifier la portée"),
               [index](Label *l) { l->scope = Label::Scope(index); });
    });

    // Role de renvoi, repris des fleches de signal d'AutoCAD Electrical.
    auto *role = new QComboBox(parent);
    role->addItem(tr("Étiquette ou renvoi simple"), int(Label::Role::Plain));
    role->addItem(tr("Flèche de signal — source"), int(Label::Role::Source));
    role->addItem(tr("Flèche de signal — destination"), int(Label::Role::Destination));
    role->setCurrentIndex(int(label->role));
    form->addRow(tr("Rôle"), role);
    connect(role, &QComboBox::currentIndexChanged, this, [this, label, scope](int index) {
        const auto chosen = Label::Role(index);
        modify(label, tr("Modifier le rôle"), [chosen](Label *l) {
            l->role = chosen;
            // Une fleche de signal renvoie a une autre page : la portee suit,
            // sinon la fleche pointerait vers rien.
            if (chosen != Label::Role::Plain)
                l->scope = Label::Scope::Project;
        });
        if (chosen != Label::Role::Plain) {
            QSignalBlocker blocker(scope);
            scope->setCurrentIndex(int(Label::Scope::Project));
        }
    });

    auto *direction = new QComboBox(parent);
    direction->addItem(tr("Droite"), int(Direction::Right));
    direction->addItem(tr("Bas"), int(Direction::Down));
    direction->addItem(tr("Gauche"), int(Direction::Left));
    direction->addItem(tr("Haut"), int(Direction::Up));
    direction->setCurrentIndex(std::max(0, direction->findData(toDegrees(label->direction))));
    form->addRow(tr("Sens"), direction);
    connect(direction, &QComboBox::currentIndexChanged, this, [this, label, direction] {
        const auto value = directionFromDegrees(direction->currentData().toInt());
        modify(label, tr("Modifier le sens"), [value](Label *l) { l->direction = value; });
    });

    // Le renvoi tel qu'il est dessine : « → 2/A3 ». C'est la meme source que
    // celle du peintre, donc l'inspecteur ne peut pas contredire le dessin.
    const QHash<QString, QString> refs =
            CrossReference::resolve(m_document->project(), m_document->netlist());
    const QString reference = refs.value(label->id());
    if (!reference.isEmpty())
        form->addRow(tr("Renvois"), readOnly(reference, parent));
}

void PropertiesPanel::buildDimensionForm(QFormLayout *form, DimensionItem *dimension)
{
    QWidget *parent = form->parentWidget();

    // La mesure, en lecture seule. C'est le coeur du type : elle se DEDUIT du
    // dessin. La montrer non modifiable dit mieux qu'une phrase pourquoi une
    // cote n'est pas un texte pose a cote.
    auto *mesure = new QLabel(tr("%1 mm").arg(dimension->measure(), 0, 'f', 2), parent);
    form->addRow(tr("Mesuré"), mesure);

    auto *genre = new QComboBox(parent);
    genre->addItem(tr("Alignée"), int(DimensionItem::Kind::Aligned));
    genre->addItem(tr("Horizontale"), int(DimensionItem::Kind::Horizontal));
    genre->addItem(tr("Verticale"), int(DimensionItem::Kind::Vertical));
    genre->setCurrentIndex(int(dimension->kind));
    form->addRow(tr("Genre"), genre);
    connect(genre, &QComboBox::currentIndexChanged, this, [this, dimension](int index) {
        modify(dimension, tr("Modifier le genre de cote"),
               [index](DimensionItem *d) { d->kind = DimensionItem::Kind(index); });
    });

    auto *decimales = new QSpinBox(parent);
    decimales->setRange(0, 3);
    decimales->setValue(dimension->decimals);
    form->addRow(tr("Décimales"), decimales);
    connect(decimales, &QSpinBox::valueChanged, this, [this, dimension](int value) {
        modify(dimension, tr("Modifier les décimales"),
               [value](DimensionItem *d) { d->decimals = value; });
    });

    auto *hauteur = lengthBox(dimension->textHeight, parent, 0.5);
    form->addRow(tr("Hauteur du texte"), hauteur);
    connect(hauteur, &QDoubleSpinBox::valueChanged, this, [this, dimension](double value) {
        modify(dimension, tr("Modifier la hauteur"),
               [value](DimensionItem *d) { d->textHeight = value; });
    });

    auto *unite = new QLineEdit(dimension->suffix, parent);
    unite->setPlaceholderText(tr("mm, cm… vide = rien"));
    form->addRow(tr("Unité"), unite);
    connect(unite, &QLineEdit::editingFinished, this, [this, dimension, unite] {
        if (dimension->suffix == unite->text())
            return;
        modify(dimension, tr("Modifier l'unité"),
               [&](DimensionItem *d) { d->suffix = unite->text(); });
    });

    // La valeur imposee est le seul cas ou une cote ne dit pas ce qu'elle
    // mesure. Elle existe pour la rupture d'echelle, et elle se declare.
    auto *impose = new QLineEdit(dimension->override, parent);
    impose->setPlaceholderText(tr("vide = la mesure"));
    form->addRow(tr("Texte imposé"), impose);
    connect(impose, &QLineEdit::editingFinished, this, [this, dimension, impose] {
        if (dimension->override == impose->text())
            return;
        modify(dimension, tr("Imposer le texte de la cote"),
               [&](DimensionItem *d) { d->override = impose->text(); });
    });
}

void PropertiesPanel::buildJunctionForm(QFormLayout *form, Junction *junction)
{
    QWidget *parent = form->parentWidget();
    auto *diameter = lengthBox(junction->diameter, parent, 0.2);
    form->addRow(tr("Diamètre"), diameter);
    connect(diameter, &QDoubleSpinBox::valueChanged, this, [this, junction](double value) {
        modify(junction, tr("Modifier une jonction"), [value](Junction *j) { j->diameter = value; });
    });
    form->addRow(tr("Position"),
                 readOnly(QStringLiteral("%1 ; %2 mm")
                                  .arg(junction->point.x(), 0, 'f', 1)
                                  .arg(junction->point.y(), 0, 'f', 1),
                          parent));
}

void PropertiesPanel::buildMultiForm(QFormLayout *form)
{
    QWidget *parent = form->parentWidget();
    Folio *folio = m_document->currentFolio();

    int symbols = 0;
    int wires = 0;
    int others = 0;
    double wireLength = 0.0;
    for (const QString &id : std::as_const(m_selection)) {
        const Entity *entity = folio->entity(id);
        if (!entity)
            continue;
        if (entity->type() == EntityType::Symbol)
            ++symbols;
        else if (const auto *wire = dynamic_cast<const Wire *>(entity)) {
            ++wires;
            wireLength += wire->length();
        } else {
            ++others;
        }
    }

    form->addRow(tr("Appareils"), readOnly(QString::number(symbols), parent));
    form->addRow(tr("Fils"), readOnly(QString::number(wires), parent));
    form->addRow(tr("Autres"), readOnly(QString::number(others), parent));
    if (wires > 0)
        form->addRow(tr("Longueur totale"),
                     readOnly(QStringLiteral("%1 mm").arg(wireLength, 0, 'f', 1), parent));
}

} // namespace dsn
