#include "ribbon.h"

#include "theme.h"

#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace dsn {

namespace {

// Le filet vertical qui separe deux panneaux. Un widget d'un pixel plutot
// qu'un QFrame::VLine : Fusion grave la seconde sur deux pixels avec un
// relief, et le panneau redevient une boite — or la regle du systeme visuel
// est « des filets, pas des boites ».
class PanelSeparator : public QWidget
{
public:
    explicit PanelSeparator(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedWidth(1);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // Le filet s'arrete avant le nom du panneau : il separe les
        // commandes, pas les etiquettes.
        p.fillRect(0, Theme::space(2), 1, height() - Theme::space(6), Theme::colors().border);
    }
};

QString shortenLabel(const QString &text)
{
    QString out = text;
    out.remove(QLatin1Char('&'));
    out.remove(QStringLiteral("…"));
    return out.trimmed();
}

} // namespace

// --------------------------------------------------------------------------
// RibbonPanel

RibbonPanel::RibbonPanel(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(Theme::space(2), Theme::space(1), Theme::space(2), 0);
    column->setSpacing(0);

    auto *content = new QWidget(this);
    content->setFixedHeight(Ribbon::kRowHeight);
    m_row = new QHBoxLayout(content);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(Theme::space(1));
    column->addWidget(content);

    m_name = new QLabel(title.toUpper(), this);
    m_name->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    Theme::engrave(m_name);
    m_name->setFixedHeight(Ribbon::kPanelHeight - Ribbon::kRowHeight - Theme::space(1));
    column->addWidget(m_name);

    setFixedHeight(Ribbon::kPanelHeight);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void RibbonPanel::closeGrid()
{
    m_grid = nullptr;
    m_small = 0;
}

QToolButton *RibbonPanel::addLarge(QAction *action, const QString &shortLabel)
{
    if (!action)
        return nullptr;
    closeGrid();

    // Le libelle du gros bouton passe par action->iconText() : c'est ce que
    // QToolButton::setDefaultAction recopie, et un setText pose apres serait
    // efface au premier changement d'etat de l'action. iconText ne touche pas
    // au libelle des menus, qui garde son mnemonique et ses points de
    // suspension.
    if (!shortLabel.isEmpty())
        action->setIconText(shortLabel);
    else if (action->iconText() == action->text())
        action->setIconText(shortenLabel(action->text()));

    auto *button = new QToolButton(this);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(Ribbon::kLargeIcon, Ribbon::kLargeIcon));
    button->setProperty("ribbonLarge", true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(Ribbon::kRowHeight);
    button->setMinimumWidth(Theme::space(13));
    m_row->addWidget(button);
    m_actions.append(action);
    return button;
}

QToolButton *RibbonPanel::addLargeMenu(QAction *action, QMenu *menu, const QString &shortLabel)
{
    QToolButton *button = addLarge(action, shortLabel);
    if (button && menu) {
        button->setMenu(menu);
        button->setPopupMode(QToolButton::MenuButtonPopup);
    }
    return button;
}

QToolButton *RibbonPanel::addSmall(QAction *action)
{
    if (!action)
        return nullptr;
    if (!m_grid) {
        auto *host = new QWidget(this);
        m_grid = new QGridLayout(host);
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setSpacing(Theme::space(1) / 2);
        m_row->addWidget(host, 0, Qt::AlignVCenter);
    }

    auto *button = new QToolButton(this);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(Ribbon::kSmallIcon, Ribbon::kSmallIcon));
    button->setProperty("ribbonSmall", true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    // La grille se remplit en colonnes, deux par colonne : la hauteur du
    // panneau reste la meme quel que soit le nombre de commandes.
    m_grid->addWidget(button, m_small % 2, m_small / 2);
    ++m_small;
    m_actions.append(action);
    return button;
}

void RibbonPanel::addControl(QWidget *widget)
{
    if (!widget)
        return;
    closeGrid();
    m_row->addWidget(widget, 0, Qt::AlignVCenter);
}

// --------------------------------------------------------------------------
// RibbonPage

RibbonPage::RibbonPage(QWidget *parent) : QWidget(parent)
{
    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(Theme::space(1), 0, Theme::space(1), 0);
    m_row->setSpacing(0);
    m_row->addStretch(1);
}

RibbonPanel *RibbonPage::addPanel(const QString &title)
{
    auto *panel = new RibbonPanel(title, this);
    // Le filet vient avant le panneau, sauf pour le premier : deux panneaux
    // se separent, le bord de la fenetre n'a rien a separer.
    const int at = m_row->count() - 1;
    if (!m_panels.isEmpty())
        m_row->insertWidget(at, new PanelSeparator(this));
    m_row->insertWidget(m_row->count() - 1, panel);
    m_panels.append(panel);
    return panel;
}

// --------------------------------------------------------------------------
// Ribbon

Ribbon::Ribbon(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto *tabRow = new QWidget(this);
    auto *tabLayout = new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(Theme::space(2), 0, Theme::space(2), 0);
    tabLayout->setSpacing(0);

    // La barre d'acces rapide vient avant les onglets : elle ne change
    // jamais, et ce qui ne change pas se pose au meme endroit.
    m_quickRow = new QHBoxLayout;
    m_quickRow->setContentsMargins(0, 0, Theme::space(2), 0);
    m_quickRow->setSpacing(0);
    tabLayout->addLayout(m_quickRow);

    m_tabs = new QTabBar(tabRow);
    m_tabs->setProperty("ribbon", true);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    tabLayout->addWidget(m_tabs);
    tabLayout->addStretch(1);

    // Le chevron de repli, a droite comme chez AutoCAD.
    m_fold = new QToolButton(tabRow);
    m_fold->setAutoRaise(true);
    m_fold->setFocusPolicy(Qt::NoFocus);
    m_fold->setProperty("ribbonFold", true);
    m_fold->setToolTip(tr("Replier le ruban"));
    connect(m_fold, &QToolButton::clicked, this, [this] { setCollapsed(!m_collapsed); });
    tabLayout->addWidget(m_fold);

    tabRow->setFixedHeight(kTabHeight);
    column->addWidget(tabRow);

    m_pages = new QStackedWidget(this);
    m_pages->setFixedHeight(kPanelHeight + kScrollAllowance);
    column->addWidget(m_pages);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < m_pages->count())
            m_pages->setCurrentIndex(index);
    });
    // Le clic sur l'onglet deja actif replie le ruban : c'est le geste
    // d'AutoCAD, et il rend la place au dessin sans aller chercher un menu.
    connect(m_tabs, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index == m_tabs->currentIndex())
            setCollapsed(!m_collapsed);
        else if (m_collapsed)
            setCollapsed(false);
    });

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    applyTheme();
}

QToolButton *Ribbon::addQuickAction(QAction *action)
{
    if (!action)
        return nullptr;
    auto *button = new QToolButton(this);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(16, 16));
    button->setProperty("ribbonQuick", true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    m_quickRow->addWidget(button);
    m_quick.append(action);
    return button;
}

RibbonPage *Ribbon::addPage(const QString &title)
{
    auto *page = new RibbonPage(m_pages);
    // Un onglet charge depasse la largeur de la fenetre sur un ecran etroit.
    // Plutot que de masquer la fin en silence — ce que faisait la barre
    // d'outils — la page defile horizontalement.
    auto *scroll = new QScrollArea(m_pages);
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pages->addWidget(scroll);

    m_tabs->addTab(title);
    m_pageList.append(page);
    m_titles.append(title);
    return page;
}

RibbonPage *Ribbon::page(int index) const
{
    return index >= 0 && index < m_pageList.size() ? m_pageList.at(index) : nullptr;
}

RibbonPage *Ribbon::page(const QString &title) const
{
    const int index = m_titles.indexOf(title);
    return index < 0 ? nullptr : m_pageList.at(index);
}

int Ribbon::pageCount() const { return int(m_pageList.size()); }

void Ribbon::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    m_pages->setVisible(!collapsed);
    m_fold->setToolTip(collapsed ? tr("Déplier le ruban") : tr("Replier le ruban"));
    applyTheme();
    updateGeometry();
    Q_EMIT collapsedChanged(collapsed);
}

void Ribbon::applyTheme()
{
    m_fold->setIcon(Icons::icon(m_collapsed ? Icons::Glyph::Down : Icons::Glyph::Up));
    update();
}

void Ribbon::paintEvent(QPaintEvent *)
{
    // Le filet du bas, seule bordure du ruban : la barre de menus porte deja
    // le sien en haut.
    QPainter p(this);
    p.fillRect(rect(), Theme::colors().window);
    p.fillRect(0, height() - 1, width(), 1, Theme::colors().border);
}

QSize Ribbon::sizeHint() const
{
    return QSize(640, m_collapsed ? kTabHeight
                                 : kTabHeight + kPanelHeight + kScrollAllowance);
}

QSize Ribbon::minimumSizeHint() const { return QSize(320, sizeHint().height()); }

} // namespace dsn
