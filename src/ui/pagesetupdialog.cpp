#include "pagesetupdialog.h"

#include "render/foliopainter.h"
#include "mainwindow.h"
#include "theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dsn {

namespace {
constexpr auto kCustomId = "CUSTOM";
}

// --------------------------------------------------------------------------
// PagePreview

PagePreview::PagePreview(const Project &project, QWidget *parent)
    : QWidget(parent), m_project(project)
{
    setMinimumSize(260, 240);
}

void PagePreview::setFolio(const Folio &folio)
{
    m_folio = folio;
    update();
}

void PagePreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Le meme style que le canevas : regler une mise en page sur une feuille
    // d'une autre couleur que celle qu'on dessine n'aide personne.
    RenderStyle style = MainWindow::buildRenderStyle();
    style.showGrid = false;
    style.showSheetShadow = true;
    style.showWireNumbers = false;
    style.showValues = false;
    style.showUnconnectedPins = false;
    painter.fillRect(rect(), style.pageBackground);

    const QRectF sheet = m_folio.sheetRect();
    if (sheet.width() <= 0.0 || sheet.height() <= 0.0)
        return;

    const double margin = 10.0;
    const double scale = std::min((width() - margin * 2) / sheet.width(),
                                  (height() - margin * 2) / sheet.height());
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(scale, scale);
    painter.translate(-sheet.center());

    // Le meme peintre que le canevas et que le PDF : l'apercu ne peut pas
    // mentir sur ce que donnera l'impression.
    FolioPainter(m_project, style).paint(painter, m_folio);
}

// --------------------------------------------------------------------------
// PageSetupDialog

// « nom = largeur », une bande par ligne. Le format tient sur une ligne parce
// qu'un cartouche de bandes se relit d'un coup d'oeil ; un tableau a deux
// colonnes demanderait trois clics pour ajouter une bande.
//
// Une ligne sans « = » est un nom sans largeur : elle prend la largeur par
// defaut. Refuser la ligne serait pire — on perdrait le nom deja tape.
QVector<FolioBand> PageSetupDialog::parseBands(const QString &text)
{
    QVector<FolioBand> bands;
    const QStringList lignes = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &ligne : lignes) {
        const int egal = ligne.lastIndexOf(QLatin1Char('='));
        FolioBand b;
        if (egal < 0) {
            b.title = ligne.trimmed();
        } else {
            b.title = ligne.left(egal).trimmed();
            bool ok = false;
            const double w = ligne.mid(egal + 1).trimmed().toDouble(&ok);
            if (ok && w > 1.0)
                b.width = w;
        }
        if (!b.title.isEmpty())
            bands.append(b);
    }
    return bands;
}

PageSetupDialog::PageSetupDialog(const Project &project, const Folio &folio, QWidget *parent)
    : QDialog(parent), m_project(project), m_folio(folio)
{
    setWindowTitle(tr("Mise en page"));

    auto *outer = new QVBoxLayout(this);
    auto *columns = new QHBoxLayout;
    outer->addLayout(columns, 1);

    auto *left = new QVBoxLayout;
    columns->addLayout(left);

    // ---- format ---------------------------------------------------------
    auto *formatBox = new QGroupBox(tr("Format de feuille"), this);
    auto *formatForm = new QFormLayout(formatBox);

    m_format = new QComboBox(formatBox);
    const auto formats = allSheetFormats();
    for (const SheetFormat &f : formats)
        m_format->addItem(f.label, f.id);
    m_format->addItem(tr("Personnalisé…"), QLatin1String(kCustomId));
    formatForm->addRow(tr("Format"), m_format);

    m_width = new QDoubleSpinBox(formatBox);
    m_width->setRange(50.0, 3000.0);
    m_width->setDecimals(1);
    m_width->setSuffix(QStringLiteral(" mm"));
    formatForm->addRow(tr("Largeur"), m_width);

    m_height = new QDoubleSpinBox(formatBox);
    m_height->setRange(50.0, 3000.0);
    m_height->setDecimals(1);
    m_height->setSuffix(QStringLiteral(" mm"));
    formatForm->addRow(tr("Hauteur"), m_height);

    m_landscape = new QRadioButton(tr("Paysage"), formatBox);
    m_portrait = new QRadioButton(tr("Portrait"), formatBox);
    auto *orientation = new QButtonGroup(this);
    orientation->addButton(m_landscape);
    orientation->addButton(m_portrait);
    auto *orientationRow = new QHBoxLayout;
    orientationRow->addWidget(m_landscape);
    orientationRow->addWidget(m_portrait);
    orientationRow->addStretch(1);
    formatForm->addRow(tr("Orientation"), orientationRow);
    left->addWidget(formatBox);

    // ---- cadre ----------------------------------------------------------
    auto *frameBox = new QGroupBox(tr("Cadre et zones de repérage"), this);
    auto *frameForm = new QFormLayout(frameBox);

    m_margin = new QDoubleSpinBox(frameBox);
    m_margin->setRange(0.0, 100.0);
    m_margin->setSuffix(QStringLiteral(" mm"));
    frameForm->addRow(tr("Marge"), m_margin);

    m_bindingMargin = new QDoubleSpinBox(frameBox);
    m_bindingMargin->setRange(0.0, 100.0);
    m_bindingMargin->setSuffix(QStringLiteral(" mm"));
    m_bindingMargin->setToolTip(tr("Marge de reliure, à gauche"));
    frameForm->addRow(tr("Reliure"), m_bindingMargin);

    m_columns = new QSpinBox(frameBox);
    m_columns->setRange(1, 40);
    frameForm->addRow(tr("Colonnes"), m_columns);

    m_rows = new QSpinBox(frameBox);
    m_rows->setRange(1, 26);
    m_rows->setToolTip(tr("Les lignes sont repérées A, B, C… : au-delà de 26 il n'y a plus de "
                          "lettre disponible"));
    frameForm->addRow(tr("Lignes"), m_rows);

    m_zoneLabels = new QCheckBox(tr("Afficher les repères de zone"), frameBox);
    frameForm->addRow(m_zoneLabels);

    // LE SENS DU REPERAGE. Ce n'est pas un detail d'affichage : tous les
    // renvois du dossier en dependent, et une planche reproduite avec l'autre
    // sens renvoie vers la mauvaise case sans que rien ne le signale.
    m_columnsRtl = new QCheckBox(tr("Colonnes numérotées de droite à gauche"), frameBox);
    m_columnsRtl->setToolTip(tr("La colonne 1 du côté du cartouche, comme sur les schémas "
                                "de boucle"));
    frameForm->addRow(m_columnsRtl);
    m_rowsBtt = new QCheckBox(tr("Lignes lettrées de bas en haut"), frameBox);
    m_rowsBtt->setToolTip(tr("La ligne A en bas de la planche"));
    frameForm->addRow(m_rowsBtt);

    // ---- les bandes de localisation --------------------------------------
    //
    // Ce que la bande veut dire : la localisation de tout ce qu'elle contient.
    // Une ligne par bande, « nom = largeur », parce que c'est ce qu'on relit
    // le plus vite — et la derniere s'étire jusqu'au bord.
    auto *bandBox = new QGroupBox(tr("Bandes de localisation"), this);
    auto *bandLayout = new QVBoxLayout(bandBox);
    auto *bandHint = new QLabel(
            tr("Une bande par ligne, « nom = largeur en mm ». La dernière s'étend jusqu'au "
               "bord. Laisser vide pour une planche sans bandes."),
            bandBox);
    bandHint->setWordWrap(true);
    bandLayout->addWidget(bandHint);
    m_bands = new QPlainTextEdit(bandBox);
    m_bands->setPlaceholderText(QStringLiteral("CHAMP = 200\nCABINET 037BJ0151 = 120"));
    m_bands->setFixedHeight(84);
    bandLayout->addWidget(m_bands);
    left->addWidget(frameBox);
    left->addWidget(bandBox);

    // ---- cartouche ------------------------------------------------------
    auto *blockBox = new QGroupBox(tr("Cartouche"), this);
    auto *blockForm = new QFormLayout(blockBox);

    m_blockWidth = new QDoubleSpinBox(blockBox);
    m_blockWidth->setRange(40.0, 400.0);
    m_blockWidth->setSuffix(QStringLiteral(" mm"));
    blockForm->addRow(tr("Largeur"), m_blockWidth);

    m_blockHeight = new QDoubleSpinBox(blockBox);
    m_blockHeight->setRange(16.0, 200.0);
    m_blockHeight->setSuffix(QStringLiteral(" mm"));
    blockForm->addRow(tr("Hauteur"), m_blockHeight);
    left->addWidget(blockBox);
    left->addStretch(1);

    // ---- apercu ---------------------------------------------------------
    auto *right = new QVBoxLayout;
    auto *previewBox = new QGroupBox(tr("Aperçu"), this);
    auto *previewLayout = new QVBoxLayout(previewBox);
    m_preview = new PagePreview(m_project, previewBox);
    previewLayout->addWidget(m_preview, 1);
    right->addWidget(previewBox, 1);

    m_applyAll = new QCheckBox(tr("Appliquer à tous les folios du projet"), this);
    m_applyAll->setToolTip(tr("Le contenu n'est pas déplacé : un dessin qui dépassait "
                              "dépassera encore"));
    right->addWidget(m_applyAll);
    columns->addLayout(right, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Appliquer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ---- valeurs de depart ----------------------------------------------
    m_updating = true;
    const int index = m_format->findData(m_folio.sheet.id);
    m_format->setCurrentIndex(index >= 0 ? index : m_format->count() - 1);
    m_width->setValue(m_folio.sheet.width);
    m_height->setValue(m_folio.sheet.height);
    (m_folio.sheet.width >= m_folio.sheet.height ? m_landscape : m_portrait)->setChecked(true);
    m_margin->setValue(m_folio.frame.margin);
    m_bindingMargin->setValue(m_folio.frame.bindingMargin);
    m_columns->setValue(m_folio.frame.columns);
    m_rows->setValue(m_folio.frame.rows);
    m_zoneLabels->setChecked(m_folio.frame.showZoneLabels);
    m_columnsRtl->setChecked(m_folio.frame.columnsRightToLeft);
    m_rowsBtt->setChecked(m_folio.frame.rowsBottomToTop);
    {
        QStringList lignes;
        for (const FolioBand &b : m_folio.bands) {
            lignes.append(QStringLiteral("%1 = %2").arg(b.title)
                                  .arg(b.width, 0, 'f', b.width == int(b.width) ? 0 : 1));
        }
        m_bands->setPlainText(lignes.join(QLatin1Char('\n')));
    }
    m_blockWidth->setValue(m_folio.frame.titleBlockWidth);
    m_blockHeight->setValue(m_folio.frame.titleBlockHeight);
    m_updating = false;

    // Le format pilote les dimensions ; les dimensions saisies a la main
    // basculent le format sur « Personnalisé ».
    connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        const QString id = m_format->currentData().toString();
        if (id != QLatin1String(kCustomId)) {
            const SheetFormat format = sheetFormatById(id);
            m_updating = true;
            const bool landscape = m_landscape->isChecked();
            m_width->setValue(landscape ? format.landscape().width : format.portrait().width);
            m_height->setValue(landscape ? format.landscape().height : format.portrait().height);
            m_updating = false;
        }
        refresh();
    });

    const auto sizeChanged = [this] {
        if (m_updating)
            return;
        m_updating = true;
        m_format->setCurrentIndex(m_format->count() - 1); // Personnalisé
        m_updating = false;
        refresh();
    };
    connect(m_width, &QDoubleSpinBox::valueChanged, this, sizeChanged);
    connect(m_height, &QDoubleSpinBox::valueChanged, this, sizeChanged);

    const auto flip = [this] {
        if (m_updating)
            return;
        const bool landscape = m_landscape->isChecked();
        const double w = m_width->value();
        const double h = m_height->value();
        if ((landscape && w >= h) || (!landscape && h >= w)) {
            refresh();
            return;
        }
        m_updating = true;
        m_width->setValue(h);
        m_height->setValue(w);
        m_updating = false;
        refresh();
    };
    connect(m_landscape, &QRadioButton::toggled, this, flip);

    for (QDoubleSpinBox *box : { m_margin, m_bindingMargin, m_blockWidth, m_blockHeight })
        connect(box, &QDoubleSpinBox::valueChanged, this, &PageSetupDialog::refresh);
    for (QSpinBox *box : { m_columns, m_rows })
        connect(box, &QSpinBox::valueChanged, this, &PageSetupDialog::refresh);
    connect(m_zoneLabels, &QCheckBox::toggled, this, &PageSetupDialog::refresh);
    connect(m_columnsRtl, &QCheckBox::toggled, this, &PageSetupDialog::refresh);
    connect(m_rowsBtt, &QCheckBox::toggled, this, &PageSetupDialog::refresh);
    connect(m_bands, &QPlainTextEdit::textChanged, this, &PageSetupDialog::refresh);

    refresh();
    resize(940, 640);
}

SheetFormat PageSetupDialog::currentFormat() const
{
    const QString id = m_format->currentData().toString();
    SheetFormat format;
    if (id == QLatin1String(kCustomId)) {
        format.id = QStringLiteral("CUSTOM");
        format.label = tr("Personnalisé — %1 × %2 mm")
                               .arg(m_width->value(), 0, 'f', 0)
                               .arg(m_height->value(), 0, 'f', 0);
    } else {
        format = sheetFormatById(id);
    }
    // La taille saisie fait toujours foi : elle est enregistree dans le
    // fichier a cote de l'identifiant, ce qui rend le folio relisible meme si
    // le format venait a disparaitre d'une version ulterieure.
    format.width = m_width->value();
    format.height = m_height->value();
    return format;
}

void PageSetupDialog::refresh()
{
    if (m_updating)
        return;
    m_preview->setFolio(result());
}

Folio PageSetupDialog::result() const
{
    Folio folio = m_folio;
    folio.sheet = currentFormat();
    folio.frame.margin = m_margin->value();
    folio.frame.bindingMargin = m_bindingMargin->value();
    folio.frame.columns = m_columns->value();
    folio.frame.rows = m_rows->value();
    folio.frame.showZoneLabels = m_zoneLabels->isChecked();
    folio.frame.columnsRightToLeft = m_columnsRtl->isChecked();
    folio.frame.rowsBottomToTop = m_rowsBtt->isChecked();
    folio.bands = parseBands(m_bands->toPlainText());
    folio.frame.titleBlockWidth = m_blockWidth->value();
    folio.frame.titleBlockHeight = m_blockHeight->value();
    return folio;
}

bool PageSetupDialog::applyToAllFolios() const { return m_applyAll->isChecked(); }

} // namespace dsn
