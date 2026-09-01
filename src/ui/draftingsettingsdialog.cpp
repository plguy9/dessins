#include "draftingsettingsdialog.h"

#include "render/foliopainter.h"
#include "theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dsn {

namespace {

// Vignette du marqueur d'un mode, dessinee par le meme code que le canevas :
// ce qu'on voit dans la boite de dialogue est exactement ce qui apparaitra
// sous le curseur.
QPixmap markerPreview(SnapMode mode, const QColor &color)
{
    constexpr int kBox = 22;
    constexpr double kRatio = 2.0;
    QPixmap pixmap(int(kBox * kRatio), int(kBox * kRatio));
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(kRatio, kRatio);
    FolioPainter::paintSnapMarker(painter, mode, QPointF(kBox / 2.0, kBox / 2.0), 13.0, color);
    painter.end();
    pixmap.setDevicePixelRatio(kRatio);
    return pixmap;
}

QString modeHint(SnapMode mode)
{
    switch (mode) {
    case SnapMode::Endpoint: return QObject::tr("Extrémité d'un fil ou d'un tracé");
    case SnapMode::Midpoint: return QObject::tr("Milieu d'un segment, d'une broche ou d'un côté");
    case SnapMode::Center: return QObject::tr("Centre d'un cercle ou d'un arc");
    case SnapMode::Node: return QObject::tr("Broche, jonction, étiquette, coude de fil");
    case SnapMode::Quadrant: return QObject::tr("Quadrant d'un cercle : 3 h, 6 h, 9 h, midi");
    case SnapMode::Intersection: return QObject::tr("Croisement de deux tracés");
    case SnapMode::Perpendicular: return QObject::tr("Pied de la perpendiculaire depuis le point courant");
    case SnapMode::Insertion: return QObject::tr("Point d'insertion d'un symbole ou d'un texte");
    case SnapMode::Extension: return QObject::tr("Dans l'axe, au-delà d'une extrémité");
    case SnapMode::Nearest: return QObject::tr("N'importe quel point du tracé");
    case SnapMode::Grid: return QObject::tr("Point de grille");
    }
    return QString();
}

} // namespace

DraftingSettingsDialog::DraftingSettingsDialog(const SnapEngine &engine, double gridStep,
                                               bool gridVisible, QWidget *parent)
    : QDialog(parent), m_engine(engine)
{
    setWindowTitle(tr("Paramètres de dessin"));

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    tabs->addTab(buildGridTab(), tr("Grille et résolution"));
    tabs->addTab(buildObjectSnapTab(), tr("Accrochage aux objets"));
    tabs->addTab(buildPolarTab(), tr("Repérage polaire"));

    m_gridStep->setValue(gridStep);
    m_gridVisible->setChecked(gridVisible);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Appliquer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(560, 520);
}

QWidget *DraftingSettingsDialog::buildGridTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *box = new QGroupBox(tr("Grille"), page);
    auto *form = new QFormLayout(box);

    m_gridVisible = new QCheckBox(tr("Afficher la grille"), box);
    m_gridVisible->setToolTip(tr("F7"));
    form->addRow(m_gridVisible);

    m_gridSnap = new QCheckBox(tr("Accrochage à la grille"), box);
    m_gridSnap->setToolTip(tr("F9"));
    m_gridSnap->setChecked(m_engine.gridSnapEnabled());
    form->addRow(m_gridSnap);

    m_gridStep = new QDoubleSpinBox(box);
    m_gridStep->setRange(0.1, 100.0);
    m_gridStep->setDecimals(2);
    m_gridStep->setSingleStep(0.25);
    m_gridStep->setSuffix(QStringLiteral(" mm"));
    form->addRow(tr("Pas de la grille"), m_gridStep);

    layout->addWidget(box);

    auto *note = new QLabel(
            tr("<p style='color:palette(mid)'>Le pas de grille est fourni par le profil "
               "métier du projet : 2,5 mm en CEI, 2,54 mm (un dixième de pouce) en ANSI, "
               "1,27 mm en électronique. Le modifier ici ne vaut que pour la session.</p>"),
            page);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    return page;
}

QWidget *DraftingSettingsDialog::buildObjectSnapTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    m_objectSnap = new QCheckBox(tr("Accrochage aux objets activé"), page);
    m_objectSnap->setToolTip(tr("F3"));
    m_objectSnap->setChecked(m_engine.objectSnapEnabled());
    QFont bold = m_objectSnap->font();
    bold.setBold(true);
    m_objectSnap->setFont(bold);
    layout->addWidget(m_objectSnap);

    auto *box = new QGroupBox(tr("Modes"), page);
    auto *grid = new QGridLayout(box);
    grid->setColumnStretch(2, 1);

    const QColor marker = Theme::colors().dark ? QColor(0xE2, 0xEE, 0x4A) : QColor(0x8A, 0x96, 0x14);

    int row = 0;
    const QList<SnapMode> modes = SnapEngine::allModes();
    for (SnapMode mode : modes) {
        if (mode == SnapMode::Grid)
            continue; // la grille a son propre onglet

        auto *preview = new QLabel(box);
        preview->setPixmap(markerPreview(mode, marker));
        preview->setFixedWidth(30);

        auto *check = new QCheckBox(snapModeName(mode), box);
        check->setChecked(m_engine.hasMode(mode));
        m_modeBoxes.insert(int(mode), check);

        auto *hint = new QLabel(modeHint(mode), box);
        hint->setStyleSheet(QStringLiteral("color: palette(mid);"));

        grid->addWidget(preview, row, 0);
        grid->addWidget(check, row, 1);
        grid->addWidget(hint, row, 2);
        ++row;
    }
    layout->addWidget(box);

    auto *buttons = new QHBoxLayout;
    auto *all = new QPushButton(tr("Tout cocher"), page);
    auto *none = new QPushButton(tr("Tout décocher"), page);
    auto *standard = new QPushButton(tr("Réglage conseillé"), page);
    standard->setToolTip(tr("Les points remarquables, sans « Proche » ni « Prolongement » "
                            "qui accrochent partout"));
    buttons->addWidget(all);
    buttons->addWidget(none);
    buttons->addWidget(standard);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(all, &QPushButton::clicked, this, [this] {
        for (QCheckBox *box : std::as_const(m_modeBoxes))
            box->setChecked(true);
    });
    connect(none, &QPushButton::clicked, this, [this] {
        for (QCheckBox *box : std::as_const(m_modeBoxes))
            box->setChecked(false);
    });
    connect(standard, &QPushButton::clicked, this, [this] {
        const SnapModes defaults = SnapEngine::defaultModes();
        for (auto it = m_modeBoxes.cbegin(); it != m_modeBoxes.cend(); ++it)
            it.value()->setChecked(defaults.testFlag(SnapMode(it.key())));
    });

    layout->addStretch(1);
    return page;
}

QWidget *DraftingSettingsDialog::buildPolarTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *box = new QGroupBox(tr("Contrainte de direction"), page);
    auto *form = new QFormLayout(box);

    m_ortho = new QCheckBox(tr("Mode ortho — horizontale et verticale seulement"), box);
    m_ortho->setToolTip(tr("F8"));
    m_ortho->setChecked(m_engine.orthoEnabled());
    form->addRow(m_ortho);

    m_polar = new QCheckBox(tr("Repérage polaire"), box);
    m_polar->setToolTip(tr("F10"));
    m_polar->setChecked(m_engine.polarEnabled());
    form->addRow(m_polar);

    m_polarIncrement = new QDoubleSpinBox(box);
    m_polarIncrement->setRange(1.0, 180.0);
    m_polarIncrement->setDecimals(1);
    m_polarIncrement->setSuffix(QStringLiteral(" °"));
    m_polarIncrement->setValue(m_engine.polarIncrement());
    form->addRow(tr("Incrément d'angle"), m_polarIncrement);

    m_tracking = new QCheckBox(tr("Repérage d'accrochage aux objets"), box);
    m_tracking->setToolTip(tr("F11"));
    m_tracking->setChecked(m_engine.trackingEnabled());
    form->addRow(m_tracking);

    layout->addWidget(box);

    auto *note = new QLabel(
            tr("<p style='color:palette(mid)'>Le mode ortho prime sur le repérage polaire, "
               "comme dans AutoCAD. Un accrochage à un objet prime sur les deux : viser une "
               "broche l'emporte toujours sur la contrainte d'angle.</p>"
               "<p style='color:palette(mid)'>Le <b>repérage d'accrochage</b> retient un point "
               "que l'on survole un instant — le milieu d'un fil, une extrémité — et fait "
               "partir de lui des traits d'alignement pointillés. On peut alors viser à son "
               "aplomb, loin de toute géométrie, ou au croisement de deux repères. Survoler "
               "à nouveau un point retenu l'oublie ; les repères sont relâchés à la fin du "
               "tracé.</p>"),
            page);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    return page;
}

SnapEngine DraftingSettingsDialog::engine() const
{
    SnapEngine result = m_engine;
    result.setObjectSnapEnabled(m_objectSnap->isChecked());
    result.setTrackingEnabled(m_tracking->isChecked());
    result.setGridSnapEnabled(m_gridSnap->isChecked());
    result.setOrthoEnabled(m_ortho->isChecked());
    result.setPolarEnabled(m_polar->isChecked());
    result.setPolarIncrement(m_polarIncrement->value());
    result.setGridStep(m_gridStep->value());
    for (auto it = m_modeBoxes.cbegin(); it != m_modeBoxes.cend(); ++it)
        result.setMode(SnapMode(it.key()), it.value()->isChecked());
    result.setMode(SnapMode::Grid, m_gridSnap->isChecked());
    return result;
}

double DraftingSettingsDialog::gridStep() const { return m_gridStep->value(); }

bool DraftingSettingsDialog::gridVisible() const { return m_gridVisible->isChecked(); }

} // namespace dsn
