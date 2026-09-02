#include "symbolpalette.h"

#include "render/foliopainter.h"
#include "theme.h"

#include <QComboBox>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace dsn {

namespace {

// La vignette de la grille est plus petite que celle de la liste : en grille
// c'est la forme qui compte et on en voit trente, en liste c'est un rappel a
// cote d'un nom qu'on lit deja.
constexpr int kGridIcon = 30;
constexpr int kListIcon = 26;
// La cellule est calee pour que quatre colonnes tiennent dans un panneau
// etroit : trois colonnes laissaient une bande vide a droite, et c'est
// justement la place qu'on cherchait a rendre au dessin.
constexpr int kGridCell = 48;
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kRecentLimit = 16;

const QString kGridKey = QStringLiteral("ui/symbolPaletteGrid");
const QString kRecentKey = QStringLiteral("ui/recentSymbols");

} // namespace

QString SymbolPalette::recentCategory()
{
    // Une valeur qui ne peut pas etre une categorie de la bibliotheque : les
    // categories viennent des JSON et ne commencent jamais par deux-points.
    return QStringLiteral("::recent");
}

SymbolPalette::SymbolPalette(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    // Marges resserrees : le panneau est un outil, pas une page.
    layout->setContentsMargins(Theme::space(1), Theme::space(1), Theme::space(1), 0);
    layout->setSpacing(Theme::space(1));

    // La recherche et la categorie tiennent sur une seule ligne. Empilees,
    // elles mangeaient soixante pixels de haut en permanence pour deux
    // reglages dont un ne sert qu'occasionnellement.
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(Theme::space(1));

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Rechercher…"));
    m_search->setClearButtonEnabled(true);
    header->addWidget(m_search, 1);

    m_viewToggle = new QToolButton(this);
    m_viewToggle->setCheckable(true);
    m_viewToggle->setAutoRaise(true);
    m_viewToggle->setFocusPolicy(Qt::NoFocus);
    header->addWidget(m_viewToggle);
    layout->addLayout(header);

    m_category = new QComboBox(this);
    layout->addWidget(m_category);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Pas de lignes alternees : la regle du systeme visuel est « des filets,
    // pas des boites », et un damier derriere des vignettes transparentes
    // fait varier le fond d'un symbole a l'autre.
    m_list->setAlternatingRowColors(false);
    layout->addWidget(m_list, 1);

    QSettings settings;
    m_grid = settings.value(kGridKey, true).toBool();
    m_recent = settings.value(kRecentKey).toStringList();
    applyViewMode();

    connect(m_search, &QLineEdit::textChanged, this, &SymbolPalette::rebuildList);
    connect(m_category, &QComboBox::currentIndexChanged, this, &SymbolPalette::rebuildList);
    connect(m_viewToggle, &QToolButton::toggled, this, [this](bool grid) { setGridMode(grid); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            Q_EMIT symbolChosen(item->data(kIdRole).toString());
    });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item)
            Q_EMIT symbolActivated(item->data(kIdRole).toString());
    });
    // La fleche vers le bas depuis la recherche entre dans la grille : on
    // tape trois lettres puis on descend, sans lacher le clavier.
    m_search->installEventFilter(this);
}

bool SymbolPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if ((key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter)
            && m_list->count() > 0) {
            if (m_list->currentRow() < 0)
                m_list->setCurrentRow(0);
            m_list->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SymbolPalette::applyViewMode()
{
    m_viewToggle->setChecked(m_grid);
    m_viewToggle->setIcon(Icons::icon(m_grid ? Icons::Glyph::ViewList : Icons::Glyph::ViewGrid));
    m_viewToggle->setToolTip(m_grid ? tr("Afficher la liste des noms")
                                    : tr("Afficher la grille de vignettes"));

    if (m_grid) {
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(kGridIcon, kGridIcon));
        m_list->setGridSize(QSize(kGridCell, kGridCell));
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setWrapping(true);
        m_list->setSpacing(2);
        m_list->setWordWrap(false);
        m_list->setUniformItemSizes(true);
    } else {
        m_list->setViewMode(QListView::ListMode);
        m_list->setIconSize(QSize(kListIcon, kListIcon));
        m_list->setGridSize(QSize());
        m_list->setResizeMode(QListView::Fixed);
        m_list->setWrapping(false);
        m_list->setSpacing(0);
        m_list->setWordWrap(false);
        m_list->setUniformItemSizes(true);
    }
}

void SymbolPalette::setGridMode(bool grid)
{
    if (m_grid == grid)
        return;
    m_grid = grid;
    QSettings().setValue(kGridKey, grid);
    applyViewMode();
    rebuildList();
}

void SymbolPalette::setLibrary(const SymbolLibrary *library)
{
    m_library = library;
    rebuildCategories();
    rebuildList();
}

void SymbolPalette::setNorm(const QString &norm)
{
    if (m_norm == norm)
        return;
    m_norm = norm;
    rebuildCategories();
    rebuildList();
}

QString SymbolPalette::currentDefinitionId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(kIdRole).toString() : QString();
}

int SymbolPalette::visibleCount() const { return m_list->count(); }

void SymbolPalette::noteUsed(const QString &definitionId)
{
    if (definitionId.isEmpty())
        return;
    m_recent.removeAll(definitionId);
    m_recent.prepend(definitionId);
    while (m_recent.size() > kRecentLimit)
        m_recent.removeLast();
    QSettings().setValue(kRecentKey, m_recent);

    // La liste ne se reconstruit que si c'est elle qu'on regarde : reordonner
    // sous les yeux de l'utilisateur ce qu'il vient de choisir lui ferait
    // perdre sa place.
    if (m_category->currentData().toString() == recentCategory())
        rebuildList();
}

QIcon SymbolPalette::renderIcon(const SymbolDefinition &definition, int pixels,
                                const RenderStyle &style)
{
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);

    const QRectF bounds = definition.bounds();
    if (bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0)
        return QIcon(pixmap);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Le symbole est mis a l'echelle de la vignette, en gardant ses
    // proportions : un contacteur tripolaire ne doit pas paraitre carre.
    const double margin = 3.0;
    const double scale = std::min((pixels - margin * 2) / bounds.width(),
                                  (pixels - margin * 2) / bounds.height());
    painter.translate(pixels / 2.0, pixels / 2.0);
    painter.scale(scale, scale);
    painter.translate(-bounds.center());

    RenderStyle iconStyle = style;
    iconStyle.symbolWidth = std::max(style.symbolWidth, 0.6 / scale);
    FolioPainter::paintDefinition(painter, definition, iconStyle);
    painter.end();

    return QIcon(pixmap);
}

void SymbolPalette::rebuildCategories()
{
    const QString previous = m_category->currentData().toString();
    m_category->blockSignals(true);
    m_category->clear();
    m_category->addItem(tr("Toutes les catégories"), QString());
    // Les recents en tete : sur un depart moteur on repose les mêmes cinq
    // symboles, et les chercher a chaque fois est le vrai cout du panneau.
    m_category->addItem(tr("Récemment utilisés"), recentCategory());
    if (m_library) {
        const QStringList categories = m_library->categories(m_norm);
        for (const QString &category : categories)
            m_category->addItem(category, category);
    }
    const int index = m_category->findData(previous);
    m_category->setCurrentIndex(index >= 0 ? index : 0);
    m_category->blockSignals(false);
}

void SymbolPalette::rebuildList()
{
    const QString previous = currentDefinitionId();
    m_list->clear();
    if (!m_library)
        return;

    const QString needle = m_search->text().trimmed();
    const QString category = m_category->currentData().toString();

    QList<const SymbolDefinition *> definitions = m_library->search(needle, m_norm);

    // Les recents gardent leur ordre d'usage, du plus recent au plus ancien :
    // c'est la seule chose qui les classe, et les remettre dans l'ordre de la
    // bibliotheque leur ferait perdre tout leur interet.
    if (category == recentCategory()) {
        QList<const SymbolDefinition *> ordered;
        for (const QString &id : m_recent) {
            for (const SymbolDefinition *definition : std::as_const(definitions)) {
                if (definition->id == id) {
                    ordered.append(definition);
                    break;
                }
            }
        }
        definitions = ordered;
    }

    const RenderStyle style = palette().color(QPalette::Window).lightness() < 128
            ? RenderStyle::screenDark()
            : RenderStyle::screen();
    const int pixels = m_grid ? kGridIcon : kListIcon;

    for (const SymbolDefinition *definition : std::as_const(definitions)) {
        if (!category.isEmpty() && category != recentCategory()
            && definition->category != category) {
            continue;
        }
        // En grille, le nom est dans l'infobulle et non sous la vignette :
        // « Disjoncteur magnétothermique tripolaire » tronque a huit
        // caracteres n'apprend rien et double la hauteur de chaque case.
        auto *item = new QListWidgetItem(renderIcon(*definition, pixels, style),
                                         m_grid ? QString() : definition->name, m_list);
        item->setData(kIdRole, definition->id);
        if (m_grid)
            item->setSizeHint(QSize(kGridCell - 4, kGridCell - 4));
        QString tip = QStringLiteral("<b>%1</b><br>%2").arg(definition->name.toHtmlEscaped(),
                                                            definition->category.toHtmlEscaped());
        if (!definition->designationPrefix.isEmpty())
            tip += tr("<br>Préfixe : %1").arg(definition->designationPrefix);
        tip += tr("<br>%n broche(s)", "", int(definition->pins.size()));
        item->setToolTip(tip);
    }

    if (!previous.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(kIdRole).toString() == previous) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
}

} // namespace dsn
