#include "titleblockeditor.h"

#include "core/documentcommands.h"
#include "document.h"
#include "render/foliopainter.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace dsn {

// --------------------------------------------------------------------------
// L'apercu.
//
// Il peint le cartouche avec le VRAI peintre, sur un projet d'exemple : c'est
// la seule facon de regler ce qui s'imprimera vraiment. Il porte en plus la
// designation et le glisser des cases — composer un cartouche est un travail
// d'oeil, pas une saisie de coordonnees.
class TitleBlockPreview : public QWidget
{
public:
    explicit TitleBlockPreview(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(520, 150);
        setMouseTracking(true);
    }

    void setContent(const TitleBlockTemplate *gabarit, const Project *modele,
                    const QMap<QString, QByteArray> *images)
    {
        m_template = gabarit;
        m_model = modele;
        m_images = images;
        update();
    }

    void setCurrent(int index)
    {
        m_current = index;
        update();
    }

    int current() const { return m_current; }

    // Signaux sans Q_OBJECT : deux fonctions posees par la boite. Le widget
    // n'est utilise que par elle, et un moc pour deux rappels serait du poids
    // pour rien.
    std::function<void(int)> onSelect;
    std::function<void()> onChanged;

protected:
    QPointF toMm(const QPoint &px) const
    {
        if (!m_template || m_scale <= 0.0)
            return {};
        return QPointF((px.x() - m_origin.x()) / m_scale, (px.y() - m_origin.y()) / m_scale);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::colors().canvas);
        if (!m_template || !m_model || m_template->width <= 0.0 || m_template->height <= 0.0)
            return;

        const double marge = 12.0;
        m_scale = std::min((width() - 2 * marge) / m_template->width,
                           (height() - 2 * marge) / m_template->height);
        if (m_scale <= 0.0)
            return;
        m_origin = QPointF((width() - m_template->width * m_scale) / 2.0,
                           (height() - m_template->height * m_scale) / 2.0);

        // Le folio d'exemple reserve exactement la place du gabarit : le
        // facteur d'echelle du peintre vaut alors 1, et ce qu'on voit est ce
        // qui s'imprime.
        Project modele = *m_model;
        modele.titleBlock = *m_template;
        modele.images = *m_images;
        Folio *folio = modele.folioCount() > 0 ? modele.folioAt(0) : modele.addFolio();
        folio->frame.titleBlockWidth = m_template->width;
        folio->frame.titleBlockHeight = m_template->height;

        RenderStyle style = RenderStyle::print();
        style.showGrid = false;
        FolioPainter peintre(modele, style);

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(m_origin);
        p.scale(m_scale, m_scale);
        // Le papier vient du theme, comme partout ailleurs : c'est un
        // aperçu de ce qui s'imprime, donc il porte la meme feuille que le
        // canevas et que le PDF.
        p.fillRect(QRectF(0, 0, m_template->width, m_template->height),
                   Theme::colors().paper);
        // Le peintre pose le cartouche au coin bas-droit du cadre : on ramene
        // ce coin sur notre origine.
        const QRectF bloc = folio->titleBlockRect();
        p.translate(-bloc.topLeft());
        peintre.paintTitleBlock(p, *folio);
        p.restore();

        if (m_current >= 0 && m_current < m_template->cells.size()) {
            const QRectF r = m_template->cells.at(m_current).rect;
            QPen accent(Theme::colors().accent);
            accent.setWidthF(2.0);
            accent.setCosmetic(true);
            p.setPen(accent);
            p.setBrush(QColor(Theme::colors().accent.red(), Theme::colors().accent.green(),
                              Theme::colors().accent.blue(), 40));
            p.drawRect(QRectF(m_origin + r.topLeft() * m_scale, r.size() * m_scale));
            // La poignee du coin bas-droit : redimensionner et deplacer sont
            // deux gestes, et rien ne dirait lequel on est en train de faire.
            const QPointF coin = m_origin + r.bottomRight() * m_scale;
            p.setBrush(Theme::colors().accent);
            p.drawRect(QRectF(coin - QPointF(4, 4), QSizeF(8, 8)));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_template || event->button() != Qt::LeftButton)
            return;
        const QPointF mm = toMm(event->pos());

        if (m_current >= 0 && m_current < m_template->cells.size()) {
            const QRectF r = m_template->cells.at(m_current).rect;
            const QPointF coin = m_origin + r.bottomRight() * m_scale;
            if ((event->pos() - coin.toPoint()).manhattanLength() < 10) {
                m_drag = Drag::Resize;
                m_grab = mm - r.bottomRight();
                return;
            }
        }

        // Du dessus vers le dessous : la derniere case posee est celle qu'on
        // voit, donc celle qu'on attend en cliquant.
        for (int i = int(m_template->cells.size()) - 1; i >= 0; --i) {
            if (m_template->cells.at(i).rect.contains(mm)) {
                m_current = i;
                if (onSelect)
                    onSelect(i);
                m_drag = Drag::Move;
                m_grab = mm - m_template->cells.at(i).rect.topLeft();
                update();
                return;
            }
        }
        m_current = -1;
        if (onSelect)
            onSelect(-1);
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_drag == Drag::None || !m_mutable || m_current < 0)
            return;
        const QPointF mm = toMm(event->pos());
        QRectF r = m_mutable->cells[m_current].rect;
        // Au demi-millimetre : un cartouche se compose sur une trame, et une
        // case posee a 12,37 mm ne s'aligne avec rien.
        auto arrondi = [](double v) { return std::round(v * 2.0) / 2.0; };
        if (m_drag == Drag::Move)
            r.moveTo(arrondi(mm.x() - m_grab.x()), arrondi(mm.y() - m_grab.y()));
        else
            r.setBottomRight(QPointF(arrondi(mm.x() - m_grab.x()), arrondi(mm.y() - m_grab.y())));
        if (r.width() < 2.0 || r.height() < 2.0)
            return;
        m_mutable->cells[m_current].rect = r;
        if (onChanged)
            onChanged();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_drag = Drag::None; }

public:
    // Le gabarit modifiable : l'apercu ecrit dedans pendant le glisser. Il est
    // donne separement du gabarit lu, pour que la boite reste seule
    // proprietaire et que l'apercu ne puisse pas en changer la structure.
    void setMutable(TitleBlockTemplate *gabarit) { m_mutable = gabarit; }

private:
    enum class Drag { None, Move, Resize };

    const TitleBlockTemplate *m_template = nullptr;
    TitleBlockTemplate *m_mutable = nullptr;
    const Project *m_model = nullptr;
    const QMap<QString, QByteArray> *m_images = nullptr;
    int m_current = -1;
    Drag m_drag = Drag::None;
    QPointF m_grab;
    double m_scale = 1.0;
    QPointF m_origin;
};

// --------------------------------------------------------------------------

TitleBlockEditor::TitleBlockEditor(Document *document, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setWindowTitle(tr("Cartouche du dossier"));
    setModal(true);
    resize(1080, 660);

    m_template = m_document->project().titleBlock;
    if (m_template.isEmpty())
        m_template = TitleBlock::standard();
    m_images = m_document->project().images;

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(Theme::space(3));

    // ---- en-tete : le gabarit et sa taille ------------------------------
    auto *head = new QHBoxLayout;
    m_builtins = new QComboBox(this);
    m_builtins->addItem(tr("Partir d'un gabarit livré…"), QString());
    for (const TitleBlockTemplate &t : TitleBlock::builtins())
        m_builtins->addItem(t.name, t.id);
    head->addWidget(m_builtins);

    m_name = new QLineEdit(m_template.name, this);
    m_name->setPlaceholderText(tr("Nom du cartouche"));
    head->addWidget(m_name, 1);

    auto taille = [&](const QString &caption, QDoubleSpinBox **box, double value) {
        auto *l = new QLabel(caption, this);
        Theme::engrave(l);
        *box = new QDoubleSpinBox(this);
        (*box)->setRange(20.0, 600.0);
        (*box)->setDecimals(1);
        (*box)->setSuffix(QStringLiteral(" mm"));
        (*box)->setValue(value);
        head->addWidget(l);
        head->addWidget(*box);
    };
    taille(tr("Largeur"), &m_width, m_template.width);
    taille(tr("Hauteur"), &m_height, m_template.height);
    layout->addLayout(head);

    // ---- corps : apercu | cases | fiche ---------------------------------
    // L'apercu prend TOUTE la largeur, au-dessus du reste. Un cartouche est
    // un objet large et plat — 330 mm sur 35 : le mettre dans une colonne le
    // reduirait a un timbre, et on ne compose pas ce qu'on ne voit pas.
    m_preview = new TitleBlockPreview(this);
    m_preview->setMutable(&m_template);
    m_preview->onSelect = [this](int index) { showCell(index); };
    m_preview->onChanged = [this] { showCell(m_current); };
    layout->addWidget(m_preview, 2);

    auto *body = new QHBoxLayout;
    body->setSpacing(Theme::space(3));

    auto *cellColumn = new QVBoxLayout;
    auto *cellsLabel = new QLabel(tr("Cases"), this);
    Theme::engrave(cellsLabel);
    cellColumn->addWidget(cellsLabel);
    m_cells = new QListWidget(this);
    cellColumn->addWidget(m_cells, 1);

    auto *addRow = new QHBoxLayout;
    struct AddSpec {
        TitleBlockCell::Kind kind;
        const char *label;
    };
    const AddSpec adds[] = {
        { TitleBlockCell::Kind::Field, QT_TR_NOOP("+ Champ") },
        { TitleBlockCell::Kind::Text, QT_TR_NOOP("+ Texte") },
        { TitleBlockCell::Kind::Table, QT_TR_NOOP("+ Table") },
        { TitleBlockCell::Kind::Image, QT_TR_NOOP("+ Image") },
    };
    for (const AddSpec &spec : adds) {
        auto *b = new QPushButton(tr(spec.label), this);
        connect(b, &QPushButton::clicked, this, [this, spec] { addCell(spec.kind); });
        addRow->addWidget(b);
    }
    cellColumn->addLayout(addRow);
    auto *del = new QPushButton(tr("Retirer la case"), this);
    connect(del, &QPushButton::clicked, this, &TitleBlockEditor::removeCell);
    cellColumn->addWidget(del);
    body->addLayout(cellColumn, 1);

    auto *sheet = new QGroupBox(tr("Case sélectionnée"), this);
    auto *form = new QFormLayout(sheet);

    m_kind = new QComboBox(sheet);
    m_kind->addItem(tr("Champ"), int(TitleBlockCell::Kind::Field));
    m_kind->addItem(tr("Texte fixe"), int(TitleBlockCell::Kind::Text));
    m_kind->addItem(tr("Table"), int(TitleBlockCell::Kind::Table));
    m_kind->addItem(tr("Image"), int(TitleBlockCell::Kind::Image));
    form->addRow(tr("Nature"), m_kind);

    auto coord = [&](const QString &caption, QDoubleSpinBox **box) {
        *box = new QDoubleSpinBox(sheet);
        (*box)->setRange(0.0, 600.0);
        (*box)->setDecimals(1);
        (*box)->setSingleStep(0.5);
        (*box)->setSuffix(QStringLiteral(" mm"));
        form->addRow(caption, *box);
    };
    coord(tr("X"), &m_x);
    coord(tr("Y"), &m_y);
    coord(tr("Largeur"), &m_w);
    coord(tr("Hauteur"), &m_h);

    m_label = new QLineEdit(sheet);
    m_label->setPlaceholderText(tr("gravé devant la valeur"));
    form->addRow(tr("Libellé"), m_label);

    // La liste des champs vient du coeur : l'ecrire ici la ferait diverger au
    // premier champ ajoute.
    m_field = new QComboBox(sheet);
    m_field->setEditable(true);
    m_field->addItem(QString(), QString());
    const QMap<QString, QString> captions = TitleBlock::fieldCaptions();
    for (auto it = captions.cbegin(); it != captions.cend(); ++it)
        m_field->addItem(QStringLiteral("%1  (%2)").arg(it.value(), it.key()), it.key());
    form->addRow(tr("Champ"), m_field);

    m_text = new QLineEdit(sheet);
    form->addRow(tr("Texte fixe"), m_text);

    m_columns = new QLineEdit(sheet);
    m_columns->setPlaceholderText(tr("NO ; DATE ; DESCRIPTION"));
    form->addRow(tr("Colonnes"), m_columns);

    m_textHeight = new QDoubleSpinBox(sheet);
    m_textHeight->setRange(1.0, 12.0);
    m_textHeight->setDecimals(1);
    m_textHeight->setSingleStep(0.2);
    form->addRow(tr("Hauteur du texte"), m_textHeight);

    m_labelHeight = new QDoubleSpinBox(sheet);
    m_labelHeight->setRange(1.0, 12.0);
    m_labelHeight->setDecimals(1);
    m_labelHeight->setSingleStep(0.2);
    form->addRow(tr("Hauteur du libellé"), m_labelHeight);

    m_layout = new QComboBox(sheet);
    m_layout->addItem(tr("Libellé en ligne"), int(TitleBlockCell::Layout::Inline));
    m_layout->addItem(tr("Libellé au-dessus"), int(TitleBlockCell::Layout::Stacked));
    form->addRow(tr("Disposition"), m_layout);

    m_align = new QComboBox(sheet);
    m_align->addItem(tr("Gauche"), int(Primitive::Align::Left));
    m_align->addItem(tr("Centré"), int(Primitive::Align::Center));
    m_align->addItem(tr("Droite"), int(Primitive::Align::Right));
    form->addRow(tr("Alignement"), m_align);

    m_border = new QCheckBox(tr("Encadrer"), sheet);
    form->addRow(QString(), m_border);

    auto *logo = new QPushButton(tr("Charger un logo…"), sheet);
    connect(logo, &QPushButton::clicked, this,
            [this] { loadImage(QStringLiteral("logo")); });
    form->addRow(QString(), logo);
    auto *seal = new QPushButton(tr("Charger un sceau…"), sheet);
    connect(seal, &QPushButton::clicked, this,
            [this] { loadImage(QStringLiteral("seal")); });
    form->addRow(QString(), seal);

    body->addWidget(sheet, 2);
    layout->addLayout(body, 3);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Appliquer au dossier"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // ---- branchements ---------------------------------------------------
    connect(m_builtins, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString id = m_builtins->itemData(index).toString();
        if (id.isEmpty())
            return;
        for (const TitleBlockTemplate &t : TitleBlock::builtins()) {
            if (t.id == id)
                setTemplate(t);
        }
    });
    connect(m_name, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_template.name = t; });
    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_template.width = v;
        m_preview->update();
    });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_template.height = v;
        m_preview->update();
    });
    connect(m_cells, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_filling)
            selectCell(row);
    });

    const QList<QWidget *> champs = { m_kind, m_x, m_y, m_w, m_h, m_label, m_field,
                                      m_text, m_columns, m_textHeight, m_labelHeight,
                                      m_layout, m_align, m_border };
    for (QWidget *w : champs) {
        if (auto *box = qobject_cast<QDoubleSpinBox *>(w))
            connect(box, &QDoubleSpinBox::valueChanged, this, [this] { pullFromForm(); });
        else if (auto *combo = qobject_cast<QComboBox *>(w))
            connect(combo, &QComboBox::currentIndexChanged, this, [this] { pullFromForm(); });
        else if (auto *edit = qobject_cast<QLineEdit *>(w))
            connect(edit, &QLineEdit::textChanged, this, [this] { pullFromForm(); });
        else if (auto *check = qobject_cast<QCheckBox *>(w))
            connect(check, &QCheckBox::toggled, this, [this] { pullFromForm(); });
    }

    m_modelProject = m_document->project();
    refresh();
    if (!m_template.cells.isEmpty())
        selectCell(0);
}

void TitleBlockEditor::refresh()
{
    m_preview->setContent(&m_template, &m_modelProject, &m_images);
    buildCellList();
    m_preview->update();
}

void TitleBlockEditor::setTemplate(const TitleBlockTemplate &gabarit)
{
    m_template = gabarit;
    m_filling = true;
    m_name->setText(gabarit.name);
    m_width->setValue(gabarit.width);
    m_height->setValue(gabarit.height);
    m_filling = false;
    m_current = -1;
    refresh();
    if (!m_template.cells.isEmpty())
        selectCell(0);
}

void TitleBlockEditor::buildCellList()
{
    m_filling = true;
    m_cells->clear();
    for (const TitleBlockCell &cell : m_template.cells) {
        QString nom;
        switch (cell.kind) {
        case TitleBlockCell::Kind::Field:
            nom = cell.label.isEmpty() ? cell.key : QStringLiteral("%1 · %2").arg(cell.label,
                                                                                 cell.key);
            break;
        case TitleBlockCell::Kind::Text: nom = cell.text.isEmpty() ? tr("(cadre)") : cell.text; break;
        case TitleBlockCell::Kind::Table: nom = tr("Table %1").arg(cell.key); break;
        case TitleBlockCell::Kind::Image: nom = tr("Image %1").arg(cell.key); break;
        }
        m_cells->addItem(nom);
    }
    m_cells->setCurrentRow(m_current);
    m_filling = false;
}

void TitleBlockEditor::selectCell(int index)
{
    m_current = index;
    m_preview->setCurrent(index);
    showCell(index);
    m_filling = true;
    m_cells->setCurrentRow(index);
    m_filling = false;
}

void TitleBlockEditor::showCell(int index)
{
    m_current = index;
    m_preview->setCurrent(index);
    const bool ok = index >= 0 && index < m_template.cells.size();
    m_filling = true;
    if (ok) {
        const TitleBlockCell &c = m_template.cells.at(index);
        m_kind->setCurrentIndex(m_kind->findData(int(c.kind)));
        m_x->setValue(c.rect.x());
        m_y->setValue(c.rect.y());
        m_w->setValue(c.rect.width());
        m_h->setValue(c.rect.height());
        m_label->setText(c.label);
        const int fieldRow = m_field->findData(c.key);
        if (fieldRow >= 0)
            m_field->setCurrentIndex(fieldRow);
        else
            m_field->setCurrentText(c.key);
        m_text->setText(c.text);
        m_columns->setText(c.columns.join(QStringLiteral(" ; ")));
        m_textHeight->setValue(c.textHeight);
        m_labelHeight->setValue(c.labelHeight);
        m_layout->setCurrentIndex(m_layout->findData(int(c.layout)));
        m_align->setCurrentIndex(m_align->findData(int(c.align)));
        m_border->setChecked(c.border);
    }
    m_filling = false;
    m_preview->update();
}

void TitleBlockEditor::pullFromForm()
{
    if (m_filling || m_current < 0 || m_current >= m_template.cells.size())
        return;
    TitleBlockCell &c = m_template.cells[m_current];
    c.kind = TitleBlockCell::Kind(m_kind->currentData().toInt());
    c.rect = QRectF(m_x->value(), m_y->value(), m_w->value(), m_h->value());
    c.label = m_label->text();
    // Le champ vient de la liste quand il y est, de la frappe sinon : un
    // bureau peut vouloir une clef a lui, et la lui refuser reviendrait a
    // figer le cartouche une seconde fois.
    const QVariant data = m_field->currentData();
    c.key = data.isValid() && !data.toString().isEmpty() ? data.toString()
                                                         : m_field->currentText().trimmed();
    if (c.kind == TitleBlockCell::Kind::Table || c.kind == TitleBlockCell::Kind::Image) {
        if (c.key.isEmpty())
            c.key = m_field->currentText().trimmed();
    }
    c.text = m_text->text();
    c.columns.clear();
    for (const QString &part : m_columns->text().split(QLatin1Char(';'), Qt::SkipEmptyParts))
        c.columns.append(part.trimmed());
    c.widths.clear();
    c.textHeight = m_textHeight->value();
    c.labelHeight = m_labelHeight->value();
    c.layout = TitleBlockCell::Layout(m_layout->currentData().toInt());
    c.align = Primitive::Align(m_align->currentData().toInt());
    c.border = m_border->isChecked();
    buildCellList();
    m_preview->update();
}

void TitleBlockEditor::addCell(TitleBlockCell::Kind kind)
{
    TitleBlockCell c;
    c.kind = kind;
    // Posee au centre, a une taille lisible : une case neuve de deux
    // millimetres serait invisible et impossible a rattraper a la souris.
    c.rect = QRectF(m_template.width / 2.0 - 20.0, m_template.height / 2.0 - 4.0, 40.0, 8.0);
    if (kind == TitleBlockCell::Kind::Table) {
        c.rect = QRectF(4.0, 4.0, 70.0, 20.0);
        c.columns = { tr("NO"), tr("DATE"), tr("DESCRIPTION") };
        c.textHeight = 1.8;
        c.align = Primitive::Align::Center;
        c.key = QStringLiteral("revisions");
    } else if (kind == TitleBlockCell::Kind::Image) {
        c.key = QStringLiteral("logo");
        c.border = false;
    } else if (kind == TitleBlockCell::Kind::Text) {
        c.text = tr("TEXTE");
        c.align = Primitive::Align::Center;
    } else {
        c.label = tr("Libellé");
        c.key = QStringLiteral("projectTitle");
    }
    m_template.cells.append(c);
    refresh();
    selectCell(int(m_template.cells.size()) - 1);
}

void TitleBlockEditor::removeCell()
{
    if (m_current < 0 || m_current >= m_template.cells.size())
        return;
    m_template.cells.remove(m_current);
    const int suivant = std::min(m_current, int(m_template.cells.size()) - 1);
    m_current = -1;
    refresh();
    if (suivant >= 0)
        selectCell(suivant);
}

void TitleBlockEditor::loadImage(const QString &key)
{
    const QString path = QFileDialog::getOpenFileName(
            this, tr("Choisir une image"), QString(),
            tr("Images (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    // L'image est EMBARQUEE, pas referencee : un logo pointe sur le disque
    // disparait des que le fichier change de poste.
    m_images.insert(key, file.readAll());
    refresh();
}

void TitleBlockEditor::apply()
{
    m_template.width = m_width->value();
    m_template.height = m_height->value();
    if (m_template.id.isEmpty())
        m_template.id = QStringLiteral("perso");
    if (m_template.name.isEmpty())
        m_template.name = tr("Cartouche du dossier");
    m_document->push(std::make_unique<ChangeTitleBlockCommand>(m_document->project(), m_template,
                                                               m_images));
}

} // namespace dsn
