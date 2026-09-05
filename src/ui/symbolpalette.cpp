#include "symbolpalette.h"

#include "render/foliopainter.h"
#include "mainwindow.h"
#include "theme.h"

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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
//
// Elle a maigri de 48 a 40 px quand les folios ont quitte la colonne
// (refonte 05) : moins d'air autour de chaque vignette, une cinquieme colonne,
// et le glyphe lui-meme n'a pas bouge — c'est le vide qu'on a repris, pas le
// dessin. Quarante-quatre laissait la cinquieme colonne sous l'ascenseur : la
// largeur utile est celle du VIEWPORT, pas celle du panneau.
constexpr int kGridCell = 40;
// La bande des recents : une rangee, et rien de plus. Elle prend sa place sur
// la grille, elle ne la repousse pas.
constexpr int kRecentBandHeight = kGridCell + 2;
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

    // LES RECENTS, EN BANDE PERMANENTE. Ils existaient deja — `noteUsed` les
    // alimente et `recentCategory()` les range — mais il fallait aller les
    // CHOISIR dans la liste des categories, ce qui coute autant que de
    // chercher le symbole. Sur cent trois symboles dont on en repose dix
    // toute la journee, c'est le raccourci le plus rentable du panneau.
    m_recentBand = new QWidget(this);
    auto *recentColumn = new QVBoxLayout(m_recentBand);
    recentColumn->setContentsMargins(0, 0, 0, 0);
    recentColumn->setSpacing(0);

    auto *recentTitle = new QLabel(tr("RÉCENTS"), m_recentBand);
    recentTitle->setProperty("zoneBand", true);
    recentTitle->setContentsMargins(Theme::space(1), 0, Theme::space(1), 0);
    Theme::engrave(recentTitle);
    recentColumn->addWidget(recentTitle);

    m_recentList = new QListWidget(m_recentBand);
    m_recentList->setProperty("symbolGrid", true);
    m_recentList->setViewMode(QListView::IconMode);
    m_recentList->setIconSize(QSize(kGridIcon, kGridIcon));
    m_recentList->setGridSize(QSize(kGridCell, kGridCell));
    m_recentList->setMovement(QListView::Static);
    m_recentList->setWrapping(false);
    m_recentList->setSpacing(0);
    m_recentList->setUniformItemSizes(true);
    m_recentList->setFixedHeight(kRecentBandHeight);
    m_recentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recentList->setSelectionMode(QAbstractItemView::NoSelection);
    // Sans cadre : un QListWidget en pose un de trois pixels que « border:
    // none » n'enleve pas — c'est le CADRE du widget, pas sa bordure de
    // feuille de style. Six pixels de large, et c'est la colonne de trop.
    m_recentList->setFrameShape(QFrame::NoFrame);
    recentColumn->addWidget(m_recentList);
    m_recentBand->hide();
    layout->addWidget(m_recentBand);

    m_allBand = new QLabel(tr("TOUS"), this);
    m_allBand->setProperty("zoneBand", true);
    m_allBand->setContentsMargins(Theme::space(1), 0, Theme::space(1), 0);
    Theme::engrave(m_allBand);
    layout->addWidget(m_allBand);

    m_list = new QListWidget(this);
    m_list->setProperty("symbolGrid", true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Pas de lignes alternees : la regle du systeme visuel est « des filets,
    // pas des boites », et un damier derriere des vignettes transparentes
    // fait varier le fond d'un symbole a l'autre.
    m_list->setAlternatingRowColors(false);
    // Sans cadre, pour la meme raison que la bande des recents : les trois
    // pixels de cadre de chaque cote coutaient la cinquieme colonne.
    m_list->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_list, 1);

    // LA CASE DE SELECTION, en bas, en grammaire de cartouche. La grille pose
    // forcement la question « quelle variante ai-je designee ? » — un contact
    // NO et un contact NF se ressemblent a trente pixels — et y repondre
    // coutait un survol. La case ne remplace pas le dessin, elle le confirme.
    auto *pick = new QWidget(this);
    auto *pickColumn = new QVBoxLayout(pick);
    pickColumn->setContentsMargins(Theme::space(1), Theme::space(1), Theme::space(1),
                                   Theme::space(1));
    pickColumn->setSpacing(0);

    auto *pickTitle = new QLabel(tr("SÉLECTION"), pick);
    pickTitle->setProperty("zoneBand", true);
    pickTitle->setContentsMargins(0, 0, 0, 0);
    Theme::engrave(pickTitle);
    pickColumn->addWidget(pickTitle);

    m_pickName = new QLabel(pick);
    m_pickName->setWordWrap(true);
    m_pickName->setProperty("pickName", true);
    pickColumn->addWidget(m_pickName);

    m_pickDetail = new QLabel(pick);
    m_pickDetail->setProperty("pickDetail", true);
    m_pickDetail->setFont(Theme::monoFont(8));
    pickColumn->addWidget(m_pickDetail);
    layout->addWidget(pick);

    QSettings settings;
    m_grid = settings.value(kGridKey, true).toBool();
    m_recent = settings.value(kRecentKey).toStringList();
    applyViewMode();

    connect(m_search, &QLineEdit::textChanged, this, &SymbolPalette::rebuildList);
    connect(m_category, &QComboBox::currentIndexChanged, this, &SymbolPalette::rebuildList);
    connect(m_viewToggle, &QToolButton::toggled, this, [this](bool grid) { setGridMode(grid); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        refreshSelectionCard();
        if (item)
            Q_EMIT symbolChosen(item->data(kIdRole).toString());
    });
    // La bande des recents ARME, elle ne selectionne pas : cliquer un recent
    // doit poser le symbole, pas deplacer le curseur de la grille.
    connect(m_recentList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            Q_EMIT symbolChosen(item->data(kIdRole).toString());
    });
    connect(m_recentList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item)
            Q_EMIT symbolActivated(item->data(kIdRole).toString());
    });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item)
            Q_EMIT symbolActivated(item->data(kIdRole).toString());
    });
    // La fleche vers le bas depuis la recherche entre dans la grille : on
    // tape trois lettres puis on descend, sans lacher le clavier.
    m_search->installEventFilter(this);
}

void SymbolPalette::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // La bande des recents montre ce qui tient : sa largeur vient de changer,
    // le nombre de vignettes aussi.
    rebuildRecent();
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
        // AUCUN espacement : la grille en pose deja un, puisque la vignette
        // occupe quatre pixels de moins que sa cellule. En ajouter deux de
        // plus coutait la cinquieme colonne — Qt les compte de chaque cote,
        // donc quatre pixels par cellule, et 210 px de viewport n'en portent
        // plus que quatre.
        m_list->setSpacing(0);
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

int SymbolPalette::gridCapacity() const
{
    // Ce que l'oeil voit sans faire defiler : colonnes x rangees de la grille
    // dans la fenetre du panneau. `visibleCount` compte ce que le filtre a
    // retenu — cent trois symboles tiennent dans le modele et cinq sur
    // l'ecran, et c'est le second chiffre qui dit si le panneau sert.
    if (!m_grid)
        return m_list->viewport()->height()
                / std::max(1, m_list->sizeHintForRow(0) > 0 ? m_list->sizeHintForRow(0)
                                                            : kListIcon + 8);
    const QSize cell = m_list->gridSize();
    if (cell.width() <= 0 || cell.height() <= 0)
        return 0;
    // Qt garde deux pixels a gauche et un peu de reserve a droite : le calcul
    // nu annoncait une colonne de plus que la grille n'en pose. Mesure faite
    // sur visualItemRect, pas deduite.
    const int columns = std::max(1, (m_list->viewport()->width() - 4) / cell.width());
    const int rows = std::max(0, m_list->viewport()->height() / cell.height());
    return columns * rows;
}

int SymbolPalette::recentVisibleCount() const
{
    return m_recentBand->isVisible() ? m_recentList->count() : 0;
}

void SymbolPalette::rebuildRecent()
{
    m_recentList->clear();
    if (!m_library || m_recent.isEmpty()) {
        m_recentBand->setVisible(false);
        return;
    }

    RenderStyle style = MainWindow::buildRenderStyle();
    const ThemeColors &c = Theme::colors();
    style.symbol = c.text;
    style.text = c.text;
    style.frame = c.text;

    // ON N'EN MONTRE QUE CE QUI TIENT. La bande ne defile pas — un ascenseur
    // dans quarante-six pixels est illisible — donc un recent pose au-dela du
    // bord serait injoignable, ce qui est pire que de ne pas le montrer. Les
    // autres restent dans la categorie « Récemment utilisés ».
    const int room = std::max(1, m_recentList->viewport()->width() / kGridCell);
    for (const QString &id : std::as_const(m_recent)) {
        if (m_recentList->count() >= room)
            break;
        const SymbolDefinition *definition = m_library->definition(id);
        if (!definition)
            continue;
        auto *item = new QListWidgetItem(renderIcon(*definition, kGridIcon, style), QString(),
                                         m_recentList);
        item->setData(kIdRole, definition->id);
        item->setSizeHint(QSize(kGridCell - 4, kGridCell - 4));
        item->setToolTip(definition->name);
    }
    m_recentBand->setVisible(m_recentList->count() > 0);
}

void SymbolPalette::refreshSelectionCard()
{
    const QString id = currentDefinitionId();
    const SymbolDefinition *definition = m_library ? m_library->definition(id) : nullptr;
    if (!definition) {
        // Rien de designe : la case se tait plutot que d'afficher un tiret et
        // une ligne vide. Elle repond a une question qu'on n'a pas encore
        // posee.
        m_pickName->setText(QStringLiteral("\u2014"));
        m_pickDetail->clear();
        m_pickDetail->hide();
        return;
    }
    m_pickDetail->show();
    m_pickName->setText(definition->name);
    // La norme et le nombre de broches : les deux choses qu'un dessinateur
    // verifie avant de poser, et que la vignette seule ne dit pas.
    m_pickDetail->setText(tr("%1  ·  %n broche(s)", "", int(definition->pins.size()))
                                  .arg(definition->norm));
}

void SymbolPalette::noteUsed(const QString &definitionId)
{
    if (definitionId.isEmpty())
        return;
    m_recent.removeAll(definitionId);
    m_recent.prepend(definitionId);
    while (m_recent.size() > kRecentLimit)
        m_recent.removeLast();
    QSettings().setValue(kRecentKey, m_recent);

    // La bande des recents, elle, se refait toujours : c'est justement ce
    // qu'elle est faite pour montrer.
    rebuildRecent();

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
    // La norme du projet en tete : « Tous les symboles — CEI » plutot que
    // « Toutes les catégories », qui est vrai mais n'apprend rien. Un
    // dessinateur qui ouvre le panneau doit voir a quelle norme il dessine.
    m_category->addItem(tr("Tous les symboles — %1").arg(m_norm == QStringLiteral("IEC")
                                                                 ? tr("CEI")
                                                                 : m_norm),
                        QString());
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

    // LES VIGNETTES DE SYMBOLES N'ONT PAS DE PAPIER SOUS ELLES.
    //
    // Elles sont peintes a meme le panneau, pas sur une feuille : leur encre
    // doit donc suivre le CHROME, pas le papier. Les faire suivre le papier
    // les rend invisibles — de l'encre presque noire sur un panneau sombre,
    // ce qui vide la palette sans un message d'erreur. C'est la seule surface
    // du logiciel ou le dessin n'est pas pose sur une feuille, et c'est pour
    // cela qu'elle merite ces trois lignes.
    RenderStyle style = MainWindow::buildRenderStyle();
    const ThemeColors &c = Theme::colors();
    style.symbol = c.text;
    style.text = c.text;
    style.frame = c.text;
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

    m_allBand->setText(tr("TOUS  ·  %1").arg(m_list->count()));
    rebuildRecent();
    refreshSelectionCard();
}

} // namespace dsn
