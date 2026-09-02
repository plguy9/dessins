#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QStyleFactory>

#include <numbers>
#include <QWidget>

namespace dsn {

namespace {

ThemeColors g_colors;
bool g_dark = true;
QHash<int, QIcon> g_iconCache;
QString g_uiFamily;
QString g_monoFamily;

// L'echelle de gris est construite comme une echelle, pas choisie teinte par
// teinte : chaque marche est un pas net au-dessus de la precedente, et la
// distance entre deux marches voisines est la meme partout. C'est ce qui
// permet de superposer les plans (fond, panneau, carte, champ) sans jamais
// avoir a poser une bordure pour les separer.
ThemeColors darkColors()
{
    ThemeColors c;
    c.dark = true;
    // Neutres tres legerement bleutes. Le bleu est celui de l'arc de la
    // marque, dilue a l'extreme : l'interface et l'icone sont de la meme
    // famille sans que l'interface soit coloree.
    c.canvas = QColor(0x0A, 0x0D, 0x0F);   // le vide derriere la feuille
    c.window = QColor(0x11, 0x15, 0x18);   // le chrome
    c.surface = QColor(0x16, 0x1B, 0x1F);  // les panneaux
    c.elevated = QColor(0x1E, 0x24, 0x29); // ce qui flotte : menus, boutons
    c.border = QColor(0x23, 0x2A, 0x30);   // le filet, presque invisible
    c.borderStrong = QColor(0x39, 0x43, 0x4A);
    c.text = QColor(0xE9, 0xEE, 0xF1);
    c.textMuted = QColor(0x87, 0x94, 0x9C);
    c.textFaint = QColor(0x5C, 0x68, 0x70); // etiquettes gravees, unites
    // Un seul accent, celui du halo de l'arc. Il ne sert qu'a designer ce qui
    // est actif ou selectionne : partout ailleurs, du gris.
    c.accent = QColor(0x1F, 0xA6, 0xE8);
    c.accentHover = QColor(0x51, 0xBE, 0xF3);
    c.accentText = QColor(0x04, 0x12, 0x1A);
    c.danger = QColor(0xE8, 0x6E, 0x60);
    c.success = QColor(0x74, 0xC1, 0x6A);
    c.warning = QColor(0xE3, 0xA9, 0x42);
    return c;
}

ThemeColors lightColors()
{
    ThemeColors c;
    c.dark = false;
    c.canvas = QColor(0xDF, 0xE3, 0xE6);
    c.window = QColor(0xF2, 0xF4, 0xF6);
    c.surface = QColor(0xFF, 0xFF, 0xFF);
    c.elevated = QColor(0xFF, 0xFF, 0xFF);
    c.border = QColor(0xE1, 0xE6, 0xEA);
    c.borderStrong = QColor(0xBF, 0xC8, 0xCE);
    c.text = QColor(0x0F, 0x15, 0x19);
    c.textMuted = QColor(0x5B, 0x69, 0x71);
    c.textFaint = QColor(0x8A, 0x96, 0x9D);
    // En clair, l'accent doit tenir sur du blanc : il descend en luminosite
    // sans changer de teinte, sinon les deux themes n'ont pas la meme marque.
    c.accent = QColor(0x0B, 0x76, 0xB8);
    c.accentHover = QColor(0x0D, 0x8B, 0xD6);
    c.accentText = QColor(0xFF, 0xFF, 0xFF);
    c.danger = QColor(0xBF, 0x38, 0x2C);
    c.success = QColor(0x2F, 0x77, 0x36);
    c.warning = QColor(0x9C, 0x6B, 0x11);
    return c;
}

QString hex(const QColor &color) { return color.name(QColor::HexRgb); }

QString rgba(const QColor &color, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(alpha);
}

// Choisit la premiere fonte reellement installee. On vise en tete les fontes
// d'interface modernes des trois systemes, puis on retombe sur ce que Qt
// propose : le logiciel ne doit jamais dependre d'une fonte a installer.
QString pickFamily(const QStringList &candidates, const QString &fallback)
{
    const QStringList available = QFontDatabase::families();
    for (const QString &name : candidates) {
        if (available.contains(name, Qt::CaseInsensitive))
            return name;
    }
    return fallback;
}

void resolveFamilies()
{
    if (!g_uiFamily.isEmpty())
        return;
    g_uiFamily = pickFamily({ QStringLiteral("Inter"),
                              QStringLiteral("Segoe UI Variable Text"),
                              QStringLiteral("Segoe UI"),
                              QStringLiteral("SF Pro Text"),
                              QStringLiteral("Noto Sans"),
                              QStringLiteral("DejaVu Sans") },
                            QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
    // Les chiffres d'un logiciel de CAO defilent : une coordonnee qui change
    // ne doit pas faire bouger celles d'a cote. D'ou une chasse fixe partout
    // ou un nombre s'affiche, et jamais ailleurs.
    g_monoFamily = pickFamily({ QStringLiteral("JetBrains Mono"),
                                QStringLiteral("Cascadia Mono"),
                                QStringLiteral("Consolas"),
                                QStringLiteral("SF Mono"),
                                QStringLiteral("DejaVu Sans Mono"),
                                QStringLiteral("Noto Sans Mono") },
                              QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
}

// Le rythme du logiciel tient a une seule mesure, et Qt la donne par le
// style : plutot que de reposer les marges dans chaque boite de dialogue —
// ou de les oublier dans la moitie —, on les fixe une fois ici. Toute
// disposition qui ne demande rien de particulier respire alors pareil.
class ArcusStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        switch (metric) {
        case PM_LayoutLeftMargin:
        case PM_LayoutRightMargin:
        case PM_LayoutTopMargin:
        case PM_LayoutBottomMargin:
            return Theme::space(3);
        case PM_LayoutHorizontalSpacing:
            return Theme::space(2);
        case PM_LayoutVerticalSpacing:
            return Theme::space(2);
        default:
            break;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

QString styleSheet(const ThemeColors &c)
{
    const int r = Theme::radius();
    return QStringLiteral(R"(
* { outline: none; }

QWidget {
    background: %WINDOW%;
    color: %TEXT%;
}

QToolTip {
    background: %ELEVATED%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    padding: 6px 9px;
}

/* --- barre de menus --------------------------------------------------
   Aucun cadre : la barre de menus est du texte pose sur le chrome, et
   seule la ligne du bas la separe de ce qui suit. */
QMenuBar {
    background: %WINDOW%;
    border-bottom: 1px solid %BORDER%;
    padding: 3px 8px;
}
QMenuBar::item {
    padding: 6px 12px;
    border-radius: 6px;
    background: transparent;
}
QMenuBar::item:selected { background: %HOVER%; }
QMenuBar::item:pressed  { background: %ACCENTSOFT%; color: %ACCENT%; }

QMenu {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: %R%px;
    padding: 7px 6px;
}
QMenu::item {
    padding: 7px 30px 7px 32px;
    border-radius: 6px;
}
QMenu::item:selected  { background: %ACCENTSOFT%; color: %ACCENT%; }
QMenu::item:disabled  { color: %FAINT%; }
QMenu::separator      { height: 1px; background: %BORDER%; margin: 6px 10px; }
QMenu::icon           { padding-left: 12px; }

/* --- barres d'outils -------------------------------------------------
   Les boutons n'ont pas de cadre au repos : la barre est une rangee de
   signes, pas une rangee de boites. Le cadre n'apparait qu'au survol. */
QToolBar {
    background: %WINDOW%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    padding: 6px 8px;
    spacing: 2px;
}
QToolBar::separator {
    width: 1px;
    background: %BORDER%;
    margin: 6px 8px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 6px 9px;
    color: %TEXT%;
}
/* Le bouton qui tasse un panneau est carre et petit : le padding general des
   QToolButton lui laisserait deux pixels de large pour une icone de
   quatorze, et elle disparaitrait. C'est le meme piege que pour les boutons
   du ruban et pour la carte de l'ecran d'accueil — une taille fixe ne survit
   pas a un padding pose par feuille de style. */
QToolButton[dockClose="true"] {
    padding: 0;
    margin: 0;
    min-width: 0;
    min-height: 0;
    border-radius: 4px;
}

QToolButton:hover    { background: %HOVER%; }
QToolButton:pressed  { background: %ACCENTSOFT%; }
QToolButton:checked  {
    background: %ACCENTSOFT%;
    color: %ACCENT%;
}
QToolButton:disabled { color: %FAINT%; }
QToolButton::menu-indicator { width: 0; height: 0; image: none; }

/* --- panneaux ancrables ----------------------------------------------
   Le titre est grave dans le panneau, pas pose dessus : petites capitales
   espacees, aucun fond, un filet dessous. Un panneau n'est plus une boite
   dans une boite mais une colonne separee par un trait. */
QDockWidget {
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
    color: %FAINT%;
}
QDockWidget::title {
    background: %SURFACE%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    padding: 9px 12px 8px 12px;
    text-align: left;
}
QDockWidget > QWidget {
    background: %SURFACE%;
    border: none;
}
QDockWidget::close-button, QDockWidget::float-button {
    background: transparent;
    border: none;
    padding: 2px;
    icon-size: 11px;
}
QDockWidget::close-button:hover, QDockWidget::float-button:hover {
    background: %HOVER%;
    border-radius: 4px;
}

/* La rainure entre deux panneaux est un filet, pas une gouttiere. */
QMainWindow::separator {
    background: %BORDER%;
    width: 1px;
    height: 1px;
}
QMainWindow::separator:hover { background: %ACCENT%; }

/* --- champs de saisie ------------------------------------------------ */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QDateEdit, QComboBox {
    background: %FIELD%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    padding: 6px 9px;
    selection-background-color: %ACCENT%;
    selection-color: %ACCENTTEXT%;
    min-height: 18px;
}
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QDateEdit:hover, QComboBox:hover {
    border-color: %BORDERSTRONG%;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus,
QDoubleSpinBox:focus, QDateEdit:focus, QComboBox:focus {
    border-color: %ACCENT%;
    background: %FIELDFOCUS%;
}
/* L'historique de la ligne de commande n'est PAS un champ de saisie : c'est
   du texte pose. Sans cette regle il herite du fond, de la bordure et des
   coins arrondis des champs, et le bandeau parait porter deux lignes de
   commande l'une au-dessus de l'autre — ce qu'un utilisateur a signale. */
QPlainTextEdit[commandHistory="true"] {
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 2px 9px 0 9px;
    color: %MUTED%;
}

QLineEdit:disabled, QComboBox:disabled { color: %FAINT%; background: %WINDOW%; }
QLineEdit[readOnly="true"] { color: %MUTED%; background: %WINDOW%; }

QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    padding: 5px;
    selection-background-color: %ACCENTSOFT%;
    selection-color: %ACCENT%;
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    background: transparent;
    border: none;
    width: 16px;
}

/* --- boutons ---------------------------------------------------------
   Trois niveaux seulement : l'action principale porte l'accent plein, les
   autres un filet, et les tertiaires rien du tout. */
QPushButton {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    padding: 7px 18px;
    min-height: 18px;
}
QPushButton:hover   { background: %HOVER%; border-color: %BORDERSTRONG%; }
QPushButton:pressed { background: %ACCENTSOFT%; }
/* Le bouton par defaut se distingue par l'aplat d'accent, pas par une
   graisse : Qt calcule la taille du bouton dans son etat normal, et un texte
   engraisse a l'affichage se retrouve rogne. */
QPushButton:default {
    background: %ACCENT%;
    border-color: %ACCENT%;
    color: %ACCENTTEXT%;
    min-width: 76px;
}
QPushButton:default:hover { background: %ACCENTHOVER%; border-color: %ACCENTHOVER%; }
QPushButton:disabled { color: %FAINT%; background: %WINDOW%; border-color: %BORDER%; }
QPushButton:flat { background: transparent; border-color: transparent; }
QPushButton:flat:hover { background: %HOVER%; }

QCheckBox, QRadioButton { spacing: 8px; padding: 2px 0; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 15px; height: 15px;
    border: 1px solid %BORDERSTRONG%;
    border-radius: 4px;
    background: %FIELD%;
}
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %ACCENT%; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: %ACCENT%;
    border-color: %ACCENT%;
}

/* --- listes et tableaux ----------------------------------------------
   Pas de lignes zebrees : elles ajoutent du bruit sans rien apprendre.
   La ligne choisie est designee par un aplat d'accent tres dilue et son
   texte prend l'accent — c'est la seule couleur de la liste. */
QListWidget, QListView, QTreeView, QTableWidget, QTableView {
    background: %SURFACE%;
    border: none;
    alternate-background-color: %SURFACE%;
    selection-background-color: %ACCENTSOFT%;
    selection-color: %ACCENT%;
    padding: 3px;
}
QListWidget::item, QListView::item, QTreeView::item {
    padding: 6px 9px;
    border: none;
    border-radius: 6px;
    margin: 1px 3px;
}
QListWidget::item:hover, QListView::item:hover, QTreeView::item:hover {
    background: %HOVER%;
}
QListWidget::item:selected, QListView::item:selected, QTreeView::item:selected {
    background: %ACCENTSOFT%;
    color: %ACCENT%;
}
QTableView { gridline-color: %BORDER%; }
QTableView::item { padding: 5px 8px; border: none; }
QTableView::item:selected { background: %ACCENTSOFT%; color: %ACCENT%; }

/* L'en-tete est une etiquette gravee, comme les titres de panneaux. */
QHeaderView { background: %SURFACE%; }
QHeaderView::section {
    background: %SURFACE%;
    color: %FAINT%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    padding: 8px 9px;
    font-size: 8pt;
    font-weight: 700;
}
QHeaderView::section:hover { color: %MUTED%; }
QTableCornerButton::section { background: %SURFACE%; border: none; }

/* --- onglets ---------------------------------------------------------- */
QTabWidget::pane { border: none; border-top: 1px solid %BORDER%; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent;
    color: %MUTED%;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 8px 15px;
    margin-right: 2px;
}
QTabBar::tab:hover    { color: %TEXT%; }
QTabBar::tab:selected { color: %ACCENT%; border-bottom-color: %ACCENT%; font-weight: 600; }

/* --- ascenseurs -------------------------------------------------------
   Fins et sans fleches : dans un logiciel de dessin, la molette et le
   panoramique font le travail ; l'ascenseur n'est qu'un reperage. */
QScrollBar:vertical   { background: transparent; width: 9px; margin: 3px 2px; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px 3px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: %SCROLL%;
    border-radius: 3px;
    min-height: 32px;
    min-width: 32px;
}
QScrollBar::handle:hover { background: %BORDERSTRONG%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* --- regroupements ----------------------------------------------------
   Un groupe se lit a son titre et au filet qui le precede : le cadre
   complet enfermait chaque reglage dans une boite de plus. La taille de
   fonte n'est pas touchee ici — Qt la propagerait a tout le contenu du
   groupe, et les champs deviendraient minuscules. */
QGroupBox {
    border: none;
    border-top: 1px solid %BORDER%;
    border-radius: 0;
    margin-top: 22px;
    padding: 12px 2px 4px 2px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 0px;
    padding: 0 10px 0 0;
    color: %MUTED%;
}

/* --- barre d'etat ------------------------------------------------------ */
QStatusBar {
    background: %WINDOW%;
    border-top: 1px solid %BORDER%;
    color: %MUTED%;
    padding: 1px 6px;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %MUTED%; padding: 2px 10px; }

/* Les mesures : chasse fixe et couleur retenue, pour que le chiffre qui
   change ne fasse pas sauter la ligne. */
QLabel[readout="true"] {
    color: %MUTED%;
    padding: 2px 12px;
    border-left: 1px solid %BORDER%;
}

/* Bascules d'aide au dessin, facon barre d'etat AutoCAD : eteintes elles
   s'effacent, allumees elles portent l'accent. */
QToolButton[statusToggle="true"] {
    color: %FAINT%;
    background: transparent;
    border: none;
    border-radius: 5px;
    padding: 4px 10px;
    margin: 0 1px;
    font-size: 8pt;
    font-weight: 700;
}
QToolButton[statusToggle="true"]:hover { background: %HOVER%; color: %TEXT%; }
QToolButton[statusToggle="true"]:checked {
    background: %ACCENTSOFT%;
    color: %ACCENT%;
}

/* --- ruban -------------------------------------------------------------
   Les boutons du ruban n'heritent pas du min-height des boutons ordinaires :
   leur hauteur vient de constantes C++ (Ribbon::kRowHeight), et un min-height
   de feuille de style reecrirait la taille minimale du widget en silence. */
QToolButton[ribbonLarge="true"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px 8px 3px 8px;
    min-height: 0;
    font-size: 8.5pt;
}
QToolButton[ribbonSmall="true"], QToolButton[ribbonQuick="true"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
    padding: 3px;
    min-height: 0;
}
QToolButton[ribbonLarge="true"]:hover, QToolButton[ribbonSmall="true"]:hover,
QToolButton[ribbonQuick="true"]:hover { background: %HOVER%; }
QToolButton[ribbonLarge="true"]:pressed, QToolButton[ribbonSmall="true"]:pressed,
QToolButton[ribbonQuick="true"]:pressed { background: %ACCENTSOFT%; }
QToolButton[ribbonLarge="true"]:checked, QToolButton[ribbonSmall="true"]:checked,
QToolButton[ribbonQuick="true"]:checked {
    background: %ACCENTSOFT%;
    color: %ACCENT%;
}
QToolButton[ribbonLarge="true"]:disabled, QToolButton[ribbonSmall="true"]:disabled,
QToolButton[ribbonQuick="true"]:disabled { color: %FAINT%; }
QToolButton[ribbonFold="true"] {
    background: transparent;
    border: none;
    padding: 4px 6px;
    min-height: 0;
}
QToolButton[ribbonFold="true"]:hover { background: %HOVER%; border-radius: 5px; }

/* Les onglets du ruban : un souligne d'accent, comme les autres onglets du
   logiciel. Pas de graisse variable — le mot changerait de largeur en
   devenant actif, et toute la rangee se decalerait. */
QTabBar[ribbon="true"]::tab {
    background: transparent;
    color: %MUTED%;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 6px 14px;
    margin: 0 1px;
    font-size: 9pt;
}
QTabBar[ribbon="true"]::tab:hover { color: %TEXT%; }
QTabBar[ribbon="true"]::tab:selected {
    color: %ACCENT%;
    border-bottom-color: %ACCENT%;
}

QSplitter::handle { background: %BORDER%; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background: %ACCENT%; }

/* --- etiquettes gravees -----------------------------------------------
   Le titre d'une section, d'une colonne ou d'un champ n'est pas du texte :
   c'est un reperage. Petit, espace, en retrait — il se lit quand on le
   cherche et disparait quand on lit le contenu. */
QLabel[engraved="true"] {
    color: %FAINT%;
    font-size: 8pt;
    font-weight: 700;
    background: transparent;
}
QLabel[hint="true"] { color: %MUTED%; }

QLabel { background: transparent; }
QScrollArea { background: transparent; border: none; }
QDialog { background: %WINDOW%; }
QDialogButtonBox { button-layout: 3; }
QProgressBar {
    background: %FIELD%;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    height: 6px;
    text-align: center;
}
QProgressBar::chunk { background: %ACCENT%; border-radius: 3px; }
)")
            .replace(QStringLiteral("%WINDOW%"), hex(c.window))
            .replace(QStringLiteral("%SURFACE%"), hex(c.surface))
            .replace(QStringLiteral("%ELEVATED%"), hex(c.elevated))
            .replace(QStringLiteral("%FIELDFOCUS%"), hex(c.dark ? c.canvas : c.surface))
            .replace(QStringLiteral("%FIELD%"), hex(c.dark ? c.canvas : c.window))
            .replace(QStringLiteral("%BORDERSTRONG%"), hex(c.borderStrong))
            .replace(QStringLiteral("%BORDER%"), hex(c.border))
            .replace(QStringLiteral("%TEXT%"), hex(c.text))
            .replace(QStringLiteral("%MUTED%"), hex(c.textMuted))
            .replace(QStringLiteral("%FAINT%"), hex(c.textFaint))
            .replace(QStringLiteral("%ACCENTHOVER%"), hex(c.accentHover))
            .replace(QStringLiteral("%ACCENTTEXT%"), hex(c.accentText))
            .replace(QStringLiteral("%ACCENTSOFT%"), rgba(c.accent, c.dark ? 40 : 28))
            .replace(QStringLiteral("%ACCENTBORDER%"), rgba(c.accent, 130))
            .replace(QStringLiteral("%ACCENT%"), hex(c.accent))
            .replace(QStringLiteral("%HOVER%"), rgba(c.text, c.dark ? 16 : 14))
            .replace(QStringLiteral("%SCROLL%"), rgba(c.text, c.dark ? 40 : 46))
            .replace(QStringLiteral("%R%"), QString::number(r));
}

} // namespace

const ThemeColors &Theme::colors() { return g_colors; }
bool Theme::isDark() { return g_dark; }

QFont Theme::uiFont(int pointSize, int weight)
{
    resolveFamilies();
    QFont font(g_uiFamily);
    font.setPointSizeF(pointSize <= 0 ? 10.0 : double(pointSize));
    font.setWeight(QFont::Weight(weight <= 0 ? int(QFont::Normal) : weight));
    // Capitales et espacement remis a plat explicitement. Qt ne propage a un
    // enfant que les attributs poses sur sa fonte : poser la fonte gravee sur
    // un panneau et la fonte d'interface sur son contenu ne suffit pas — la
    // mise en capitales, que uiFont ne mentionnait pas, continuait de
    // descendre, et toute la liste des symboles se lisait en majuscules.
    font.setCapitalization(QFont::MixedCase);
    font.setLetterSpacing(QFont::PercentageSpacing, 100.0);
    return font;
}

QFont Theme::monoFont(double pointSize)
{
    resolveFamilies();
    QFont font(g_monoFamily);
    font.setStyleHint(QFont::Monospace);
    font.setPointSizeF(pointSize <= 0.0 ? 9.5 : pointSize);
    return font;
}

QFont Theme::engravedFont(double pointSize)
{
    // Petites capitales espacees. Qt n'a pas de « text-transform » en feuille
    // de style : la mise en capitales se fait donc dans le code, et seul
    // l'espacement se regle ici.
    QFont font = uiFont(0, int(QFont::DemiBold));
    font.setPointSizeF(pointSize <= 0.0 ? 8.0 : pointSize);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.9);
    font.setCapitalization(QFont::AllUppercase);
    return font;
}

void Theme::engrave(QWidget *widget)
{
    if (!widget)
        return;
    widget->setProperty("engraved", true);
    widget->setFont(engravedFont());
}

void Theme::apply(QApplication &app, bool dark)
{
    g_dark = dark;
    g_colors = dark ? darkColors() : lightColors();
    Icons::invalidate();
    resolveFamilies();

    // Fusion sert de base : c'est le seul style Qt dont le rendu ne depend pas
    // du bureau, donc le seul qui donne la meme interface sur les trois
    // systemes vises.
    app.setStyle(new ArcusStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    app.setFont(uiFont(10));

    QPalette palette;
    palette.setColor(QPalette::Window, g_colors.window);
    palette.setColor(QPalette::WindowText, g_colors.text);
    palette.setColor(QPalette::Base, dark ? g_colors.canvas : g_colors.window);
    palette.setColor(QPalette::AlternateBase, g_colors.surface);
    palette.setColor(QPalette::Text, g_colors.text);
    palette.setColor(QPalette::PlaceholderText, g_colors.textFaint);
    palette.setColor(QPalette::Button, g_colors.elevated);
    palette.setColor(QPalette::ButtonText, g_colors.text);
    palette.setColor(QPalette::Highlight, g_colors.accent);
    palette.setColor(QPalette::HighlightedText, g_colors.accentText);
    palette.setColor(QPalette::ToolTipBase, g_colors.elevated);
    palette.setColor(QPalette::ToolTipText, g_colors.text);
    palette.setColor(QPalette::Link, g_colors.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, g_colors.textFaint);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, g_colors.textFaint);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, g_colors.textFaint);
    app.setPalette(palette);

    app.setStyleSheet(styleSheet(g_colors));
}


// ==========================================================================
// Icones

void Icons::invalidate() { g_iconCache.clear(); }

QIcon Icons::appIcon()
{
    // La marque Arcus : un arc de courant qui saute entre deux bornes.
    //
    // L'arc est ce que le mot dit, et c'est aussi ce qu'un schema decrit —
    // le chemin que prend le courant d'un point a un autre. Le trace est
    // blanc a coeur et bleu au halo, comme un vrai arc electrique : c'est la
    // seule licence prise sur la geometrie, et elle est physique.
    QIcon icon;
    for (const int size : { 16, 32, 48, 64, 128, 256 }) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        const double s = size / 24.0;
        p.scale(s, s);

        // Fond sombre : un arc ne se voit que contre la nuit.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x11, 0x17, 0x1C));
        p.drawRoundedRect(QRectF(0.6, 0.6, 22.8, 22.8), 5.4, 5.4);

        // L'arc et ses bornes forment un ensemble centre sur le carre : pose
        // sur l'axe median, il laisserait toute la moitie basse vide.
        const QPointF left(5.4, 15.2);
        const QPointF right(18.6, 15.2);

        // Les deux bornes, et leurs amorces de conducteur : sans elles l'arc
        // flotte, et c'est justement d'un point a un autre qu'il saute.
        QPen lead(QColor(0x7E, 0x8E, 0x99));
        lead.setWidthF(1.5);
        lead.setCapStyle(Qt::RoundCap);
        p.setPen(lead);
        p.drawLine(QPointF(2.8, 15.2), left);
        p.drawLine(right, QPointF(21.2, 15.2));

        // Le halo, puis le coeur : deux passes du meme arc, la seconde plus
        // fine et plus claire. C'est ce qui lui donne sa chaleur a 16 pixels.
        QPainterPath arc;
        arc.moveTo(left);
        arc.cubicTo(QPointF(8.0, 4.6), QPointF(16.0, 4.6), right);

        QPen halo(QColor(0x18, 0xA0, 0xE0));
        halo.setWidthF(3.4);
        halo.setCapStyle(Qt::RoundCap);
        p.setPen(halo);
        p.drawPath(arc);

        QPen core(QColor(0xEC, 0xF7, 0xFF));
        core.setWidthF(1.3);
        core.setCapStyle(Qt::RoundCap);
        p.setPen(core);
        p.drawPath(arc);

        // Les bornes par-dessus : elles ferment le trajet.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xEC, 0xF7, 0xFF));
        p.drawEllipse(left, 1.6, 1.6);
        p.drawEllipse(right, 1.6, 1.6);
        p.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QIcon Icons::icon(Glyph glyph, bool dark)
{
    // Les deux encres de l'echelle, prises dans la palette : une icone doit
    // avoir exactement la couleur du texte a cote d'elle.
    return icon(glyph, dark ? darkColors().text : lightColors().text);
}

QIcon Icons::icon(Glyph glyph, const QColor &color)
{
    const QColor stroke = color.isValid() ? color : g_colors.text;
    const int key = int(glyph) * 31 + int(stroke.rgb() & 0xFFFFFF);
    const auto cached = g_iconCache.constFind(key);
    if (cached != g_iconCache.constEnd())
        return cached.value();

    // Toutes les icones sont tracees dans une boite de 24 unites. Le facteur de
    // densite est pose apres le trace : le poser avant le ferait appliquer deux
    // fois, une par QPainter et une par la mise a l'echelle, et l'icone
    // sortirait rognee au quart.
    constexpr int kBox = 24;
    constexpr double kRatio = 2.0;

    QPixmap pixmap(int(kBox * kRatio), int(kBox * kRatio));
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(kRatio, kRatio);

    QPen pen(stroke);
    pen.setWidthF(1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    auto fill = [&](const QColor &c) {
        p.setBrush(c);
        p.setPen(Qt::NoPen);
    };
    auto strokeOnly = [&] {
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    };

    switch (glyph) {
    case Glyph::New:
        p.drawPolyline(QPolygonF({ { 6, 3 }, { 14, 3 }, { 19, 8 }, { 19, 21 }, { 6, 21 }, { 6, 3 } }));
        p.drawPolyline(QPolygonF({ { 14, 3 }, { 14, 8 }, { 19, 8 } }));
        break;
    case Glyph::Open:
        p.drawPolyline(QPolygonF({ { 3, 19 }, { 3, 6 }, { 9, 6 }, { 11, 9 }, { 19, 9 } }));
        p.drawPolyline(QPolygonF({ { 3, 19 }, { 6, 12 }, { 22, 12 }, { 19, 19 }, { 3, 19 } }));
        break;
    case Glyph::Save:
        p.drawPolyline(QPolygonF({ { 4, 4 }, { 17, 4 }, { 20, 7 }, { 20, 20 }, { 4, 20 }, { 4, 4 } }));
        p.drawRect(QRectF(8, 4, 8, 6));
        p.drawRect(QRectF(7, 13, 10, 7));
        break;
    case Glyph::Print:
        p.drawPolyline(QPolygonF({ { 7, 9 }, { 7, 3 }, { 17, 3 }, { 17, 9 } }));
        p.drawRect(QRectF(3, 9, 18, 8));
        p.drawPolyline(QPolygonF({ { 7, 14 }, { 7, 21 }, { 17, 21 }, { 17, 14 } }));
        break;
    // Les trois exports partageaient un glyphe. Trois commandes differentes
    // sous la meme icone rendent la barre illisible : chacune porte donc, dans
    // la meme feuille, la marque de ce qu'elle produit — un aplat pour la page
    // imprimee, des sommets pour le vectoriel, une grille pour le tableau.
    case Glyph::ExportPdf:
        p.drawPolyline(QPolygonF({ { 5, 3 }, { 14, 3 }, { 19, 8 }, { 19, 21 }, { 5, 21 },
                                   { 5, 3 } }));
        p.drawPolyline(QPolygonF({ { 14, 3 }, { 14, 8 }, { 19, 8 } }));
        fill(stroke);
        p.drawRect(QRectF(8, 13, 8, 5));
        strokeOnly();
        break;
    case Glyph::ExportDxf:
        p.drawPolyline(QPolygonF({ { 5, 3 }, { 14, 3 }, { 19, 8 }, { 19, 21 }, { 5, 21 },
                                   { 5, 3 } }));
        p.drawPolyline(QPolygonF({ { 14, 3 }, { 14, 8 }, { 19, 8 } }));
        p.drawPolyline(QPolygonF({ { 8, 18 }, { 11, 12 }, { 14, 16 }, { 17, 11 } }));
        fill(stroke);
        for (const QPointF &v : { QPointF(8, 18), QPointF(11, 12), QPointF(14, 16),
                                  QPointF(17, 11) })
            p.drawEllipse(v, 1.1, 1.1);
        strokeOnly();
        break;
    case Glyph::ExportCsv:
        p.drawPolyline(QPolygonF({ { 5, 3 }, { 14, 3 }, { 19, 8 }, { 19, 21 }, { 5, 21 },
                                   { 5, 3 } }));
        p.drawPolyline(QPolygonF({ { 14, 3 }, { 14, 8 }, { 19, 8 } }));
        p.drawRect(QRectF(8, 12, 8, 6));
        p.drawLine(QPointF(12, 12), QPointF(12, 18));
        p.drawLine(QPointF(8, 15), QPointF(16, 15));
        break;
    case Glyph::Undo:
        p.drawArc(QRectF(4, 8, 16, 13), 0, 180 * 16);
        p.drawPolyline(QPolygonF({ { 1.0, 11.0 }, { 4.0, 14.8 }, { 7.8, 12.4 } }));
        break;
    case Glyph::Redo:
        p.drawArc(QRectF(4, 8, 16, 13), 0, 180 * 16);
        p.drawPolyline(QPolygonF({ { 23.0, 11.0 }, { 20.0, 14.8 }, { 16.2, 12.4 } }));
        break;
    case Glyph::Copy:
        p.drawRect(QRectF(8, 8, 12, 13));
        p.drawPolyline(QPolygonF({ { 5, 16 }, { 4, 16 }, { 4, 3 }, { 16, 3 }, { 16, 5 } }));
        break;
    case Glyph::Paste:
        p.drawPolyline(QPolygonF({ { 9, 4 }, { 5, 4 }, { 5, 21 }, { 19, 21 }, { 19, 4 }, { 15, 4 } }));
        p.drawRect(QRectF(9, 2, 6, 4));
        break;
    case Glyph::PasteKeepTags:
        // Le meme presse-papiers, avec l'etiquette qui reste accrochee : c'est
        // exactement ce que la commande promet de ne pas toucher.
        p.drawPolyline(QPolygonF({ { 9, 4 }, { 5, 4 }, { 5, 21 }, { 14, 21 } }));
        p.drawPolyline(QPolygonF({ { 15, 4 }, { 19, 4 }, { 19, 11 } }));
        p.drawRect(QRectF(9, 2, 6, 4));
        p.drawPolygon(QPolygonF({ { 13, 14 }, { 20, 14 }, { 22, 17.5 }, { 20, 21 }, { 13, 21 } }));
        break;
    case Glyph::Delete:
        p.drawLine(QPointF(4, 6), QPointF(20, 6));
        p.drawPolyline(QPolygonF({ { 6, 6 }, { 7, 21 }, { 17, 21 }, { 18, 6 } }));
        p.drawPolyline(QPolygonF({ { 9, 6 }, { 9, 3 }, { 15, 3 }, { 15, 6 } }));
        break;

    // --- outils de dessin -------------------------------------------------
    case Glyph::Select:
        // Une fleche de pointeur, remplie : c'est le geste de base.
        fill(stroke);
        p.drawPolygon(QPolygonF({ { 6, 3 }, { 6, 18 }, { 10, 14.5 }, { 12.5, 20.5 },
                                  { 15, 19.4 }, { 12.6, 13.6 }, { 17.5, 13.2 } }));
        strokeOnly();
        break;
    case Glyph::Wire:
        // Le trace d'un fil coude : deux segments orthogonaux et leurs bornes.
        p.drawPolyline(QPolygonF({ { 3, 6 }, { 12, 6 }, { 12, 18 }, { 21, 18 } }));
        fill(stroke);
        p.drawEllipse(QPointF(3, 6), 1.8, 1.8);
        p.drawEllipse(QPointF(21, 18), 1.8, 1.8);
        strokeOnly();
        break;
    case Glyph::Collapse:
        // Un chevron vers la gauche contre un montant : le panneau se tasse
        // sur le bord. La direction dit ou il part.
        p.drawPolyline(QPolygonF({ { 14, 5 }, { 7, 12 }, { 14, 19 } }));
        p.drawLine(QPointF(18, 4), QPointF(18, 20));
        break;
    case Glyph::ViewGrid:
        // Neuf cases et non quatre : a quatre, le dessin ne se distinguait
        // plus de celui de la palette de symboles — le test l'a releve.
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column)
                p.drawRect(QRectF(4 + column * 6.0, 4 + row * 6.0, 4.0, 4.0));
        }
        break;
    case Glyph::ViewList:
        // Trois lignes avec leur puce : une liste de noms.
        for (int i = 0; i < 3; ++i) {
            const double y = 6.0 + i * 6.0;
            fill(stroke);
            p.drawEllipse(QPointF(5, y), 1.4, 1.4);
            strokeOnly();
            p.drawLine(QPointF(9, y), QPointF(21, y));
        }
        break;
    case Glyph::WireTypes:
        // Trois fils et leur pastille de couleur : le type de fil, c'est
        // d'abord une couleur qu'on lit sur le folio.
        p.drawLine(QPointF(9, 6), QPointF(21, 6));
        p.drawLine(QPointF(9, 12), QPointF(21, 12));
        p.drawLine(QPointF(9, 18), QPointF(21, 18));
        fill(stroke);
        p.drawEllipse(QPointF(4.5, 6), 2.2, 2.2);
        p.drawEllipse(QPointF(4.5, 12), 2.2, 2.2);
        p.drawEllipse(QPointF(4.5, 18), 2.2, 2.2);
        break;
    case Glyph::SaveAs:
        // La disquette d'Enregistrer, avec le plus qui dit « une autre ».
        p.drawPolyline(QPolygonF({ { 4, 4 }, { 15, 4 }, { 19, 8 }, { 19, 14 } }));
        p.drawPolyline(QPolygonF({ { 4, 4 }, { 4, 20 }, { 12, 20 } }));
        p.drawRect(QRectF(7, 4, 8, 5));
        p.drawLine(QPointF(15, 18), QPointF(22, 18));
        p.drawLine(QPointF(18.5, 14.5), QPointF(18.5, 21.5));
        break;
    case Glyph::TagFormat:
        // Une etiquette et le gabarit qui la fabrique : %F%N, le format, pas
        // le geste de reperer.
        p.drawPolygon(QPolygonF({ { 3, 7 }, { 15, 7 }, { 20, 12 }, { 15, 17 }, { 3, 17 } }));
        fill(stroke);
        p.drawEllipse(QPointF(15.5, 12), 1.3, 1.3);
        strokeOnly();
        p.drawLine(QPointF(6, 20), QPointF(18, 20));
        break;
    case Glyph::ZoomWindow:
        // La loupe d'un zoom, mais posee sur une fenetre : c'est le cadre
        // qu'on tire, pas le pas de zoom.
        p.drawRect(QRectF(3, 5, 12, 10));
        p.drawEllipse(QPointF(15, 15), 5.0, 5.0);
        p.drawLine(QPointF(18.6, 18.6), QPointF(22, 22));
        break;
    case Glyph::MoveComponent:
        // Un appareil et ses deux fils qui le suivent : c'est exactement ce
        // que la commande promet, et ce qui la distingue de Deplacer.
        p.drawRect(QRectF(8, 8, 8, 8));
        p.drawLine(QPointF(2, 12), QPointF(8, 12));
        p.drawLine(QPointF(16, 12), QPointF(22, 12));
        p.drawPolyline(QPolygonF({ { 10, 20 }, { 12, 22.5 }, { 14, 20 } }));
        p.drawLine(QPointF(12, 17), QPointF(12, 22));
        break;
    case Glyph::WireBus:
        // Trois conducteurs paralleles qui tournent ensemble : c'est ce que la
        // commande dessine, et le coude dit qu'ils restent paralleles.
        p.drawPolyline(QPolygonF({ { 2, 6 }, { 13, 6 }, { 13, 21 } }));
        p.drawPolyline(QPolygonF({ { 2, 11 }, { 17, 11 }, { 17, 21 } }));
        p.drawPolyline(QPolygonF({ { 2, 16 }, { 21, 16 }, { 21, 21 } }));
        break;
    case Glyph::WireTypeApply:
        // Un fil deja trace, et le pinceau qui lui donne son type.
        p.drawLine(QPointF(2, 18), QPointF(14, 18));
        p.drawPolygon(QPolygonF({ { 15, 4 }, { 21, 4 }, { 21, 12 }, { 15, 12 } }));
        p.drawPolyline(QPolygonF({ { 17, 12 }, { 17, 17 }, { 19, 17 }, { 19, 12 } }));
        p.drawLine(QPointF(18, 17), QPointF(18, 21));
        break;
    case Glyph::LockTag:
        // Le cadenas ferme, anse rabattue : le repere ne bougera plus.
        p.drawRect(QRectF(5, 11, 14, 10));
        p.drawArc(QRectF(8, 3, 8, 12), 0, 180 * 16);
        break;
    case Glyph::UnlockTag:
        // Le meme cadenas, anse ouverte et decalee sur le cote. Une anse
        // seulement entrouverte au-dessus du corps ne se distingue plus de
        // l'anse fermee a seize pixels — la difference doit se voir a la
        // silhouette, pas au degre d'ouverture.
        p.drawRect(QRectF(3, 11, 13, 10));
        p.drawArc(QRectF(11, 3, 9, 12), 0, 180 * 16);
        p.drawLine(QPointF(20, 9), QPointF(20, 12));
        break;
    case Glyph::Junction:
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        p.drawLine(QPointF(12, 12), QPointF(12, 21));
        fill(stroke);
        p.drawEllipse(QPointF(12, 12), 3.0, 3.0);
        strokeOnly();
        break;
    case Glyph::LabelTag:
        p.drawPolygon(QPolygonF({ { 3, 12 }, { 7, 7 }, { 21, 7 }, { 21, 17 }, { 7, 17 } }));
        p.drawLine(QPointF(11, 12), QPointF(17, 12));
        break;
    case Glyph::Text:
        p.drawLine(QPointF(5, 5), QPointF(19, 5));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(8, 19), QPointF(16, 19));
        break;
    case Glyph::SymbolPlace:
        p.drawRect(QRectF(8, 8, 8, 8));
        p.drawLine(QPointF(3, 12), QPointF(8, 12));
        p.drawLine(QPointF(16, 12), QPointF(21, 12));
        break;
    case Glyph::Rotate:
        // Un tour presque complet, ouvert en haut a droite pour laisser passer
        // la pointe : c'est ce qui distingue une rotation d'un simple cercle.
        p.drawArc(QRectF(4, 4, 16, 16), 55 * 16, 290 * 16);
        p.drawPolyline(QPolygonF({ { 12.5, 2.2 }, { 16.4, 5.4 }, { 12.6, 8.4 } }));
        break;
    case Glyph::Mirror:
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.drawPolygon(QPolygonF({ { 9, 7 }, { 9, 17 }, { 4, 17 } }));
        p.drawPolygon(QPolygonF({ { 15, 7 }, { 15, 17 }, { 20, 17 } }));
        break;
    case Glyph::Highlight:
        p.drawPolyline(QPolygonF({ { 3, 17 }, { 9, 17 }, { 12, 7 }, { 15, 17 }, { 21, 17 } }));
        break;

    // --- vue --------------------------------------------------------------
    case Glyph::ZoomIn:
    case Glyph::ZoomOut:
        p.drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
        p.drawLine(QPointF(15.5, 15.5), QPointF(21, 21));
        p.drawLine(QPointF(7.5, 10.5), QPointF(13.5, 10.5));
        if (glyph == Glyph::ZoomIn)
            p.drawLine(QPointF(10.5, 7.5), QPointF(10.5, 13.5));
        break;
    case Glyph::ZoomFit:
        p.drawRect(QRectF(3.5, 5.5, 17, 13));
        p.drawPolyline(QPolygonF({ { 7, 9 }, { 7, 12 }, { 10, 12 } }));
        p.drawPolyline(QPolygonF({ { 17, 15 }, { 17, 12 }, { 14, 12 } }));
        break;
    case Glyph::Grid:
        for (int i = 1; i <= 2; ++i) {
            p.drawLine(QPointF(4 + i * 5.33, 4), QPointF(4 + i * 5.33, 20));
            p.drawLine(QPointF(4, 4 + i * 5.33), QPointF(20, 4 + i * 5.33));
        }
        p.drawRect(QRectF(4, 4, 16, 16));
        break;
    case Glyph::Snap:
        p.drawLine(QPointF(3, 12), QPointF(9, 12));
        p.drawLine(QPointF(15, 12), QPointF(21, 12));
        p.drawLine(QPointF(12, 3), QPointF(12, 9));
        p.drawLine(QPointF(12, 15), QPointF(12, 21));
        fill(stroke);
        p.drawEllipse(QPointF(12, 12), 2.4, 2.4);
        strokeOnly();
        break;

    case Glyph::Tracking: {
        // Le repere acquis et son alignement : une petite croix pleine, et les
        // deux traits pointilles qui en partent. C'est exactement ce que le
        // dessin montre a l'ecran quand le mode est actif.
        p.drawLine(QPointF(4, 6), QPointF(10, 6));
        p.drawLine(QPointF(7, 3), QPointF(7, 9));
        QPen dotted = p.pen();
        dotted.setStyle(Qt::DotLine);
        p.setPen(dotted);
        p.drawLine(QPointF(7, 6), QPointF(7, 18));
        p.drawLine(QPointF(7, 18), QPointF(20, 18));
        strokeOnly();
        fill(stroke);
        p.drawEllipse(QPointF(18, 18), 2.0, 2.0);
        strokeOnly();
        break;
    }

    case Glyph::Palette2: {
        // La palette de commandes : un champ de recherche et deux lignes de
        // resultats. Elle doit se reconnaitre sans legende.
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
        p.drawLine(QPointF(3, 10), QPointF(21, 10));
        p.drawLine(QPointF(6.5, 7), QPointF(13, 7));
        p.drawLine(QPointF(6.5, 14), QPointF(17, 14));
        p.drawLine(QPointF(6.5, 17), QPointF(14, 17));
        break;
    }

    // --- projet -----------------------------------------------------------
    case Glyph::Renumber:
        p.drawLine(QPointF(4, 7), QPointF(20, 7));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.drawLine(QPointF(4, 17), QPointF(20, 17));
        fill(stroke);
        p.drawEllipse(QPointF(2.4, 7), 1.2, 1.2);
        p.drawEllipse(QPointF(2.4, 12), 1.2, 1.2);
        p.drawEllipse(QPointF(2.4, 17), 1.2, 1.2);
        strokeOnly();
        break;
    case Glyph::Check:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawPolyline(QPolygonF({ { 8, 12.4 }, { 11, 15.4 }, { 16.4, 9 } }));
        break;
    case Glyph::Info:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        fill(stroke);
        p.drawEllipse(QPointF(12, 7.8), 1.1, 1.1);
        strokeOnly();
        break;
    case Glyph::Palette:
        p.drawRect(QRectF(4, 4, 7, 7));
        p.drawRect(QRectF(13, 4, 7, 7));
        p.drawRect(QRectF(4, 13, 7, 7));
        p.drawRect(QRectF(13, 13, 7, 7));
        break;
    case Glyph::Folios:
        p.drawRect(QRectF(3, 6, 12, 15));
        p.drawPolyline(QPolygonF({ { 7, 4 }, { 19, 4 }, { 19, 18 } }));
        break;
    case Glyph::Properties:
        p.drawLine(QPointF(4, 8), QPointF(20, 8));
        p.drawLine(QPointF(4, 16), QPointF(20, 16));
        fill(stroke);
        p.drawEllipse(QPointF(9, 8), 2.6, 2.6);
        p.drawEllipse(QPointF(15, 16), 2.6, 2.6);
        strokeOnly();
        break;
    case Glyph::Reports:
        p.drawRect(QRectF(4, 4, 16, 16));
        p.drawLine(QPointF(4, 9), QPointF(20, 9));
        p.drawLine(QPointF(10, 9), QPointF(10, 20));
        break;
    case Glyph::Plus:
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Glyph::Minus:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Glyph::Duplicate:
        p.drawRect(QRectF(8, 8, 12, 12));
        p.drawPolyline(QPolygonF({ { 5, 16 }, { 4, 16 }, { 4, 4 }, { 16, 4 }, { 16, 5 } }));
        break;
    case Glyph::Up:
        p.drawLine(QPointF(12, 19), QPointF(12, 6));
        p.drawPolyline(QPolygonF({ { 6, 12 }, { 12, 6 }, { 18, 12 } }));
        break;
    case Glyph::Down:
        p.drawLine(QPointF(12, 5), QPointF(12, 18));
        p.drawPolyline(QPolygonF({ { 6, 12 }, { 12, 18 }, { 18, 12 } }));
        break;
    case Glyph::Edit:
        p.drawPolyline(QPolygonF({ { 4, 20 }, { 4, 16 }, { 15, 5 }, { 19, 9 }, { 8, 20 }, { 4, 20 } }));
        p.drawLine(QPointF(13, 7), QPointF(17, 11));
        break;
    case Glyph::Count:
        break;
    case Glyph::Theme:
        p.drawEllipse(QPointF(12, 12), 7.5, 7.5);
        fill(stroke);
        p.drawChord(QRectF(4.5, 4.5, 15, 15), 90 * 16, 180 * 16);
        strokeOnly();
        break;
    case Glyph::Scale:
        // Un petit carre, un grand carre, et la diagonale qui les relie :
        // l'homothetie se lit d'un coup, y compris a seize pixels.
        p.drawRect(QRectF(3, 13, 8, 8));
        p.drawRect(QRectF(9, 3, 12, 12));
        p.drawLine(QPointF(3, 21), QPointF(21, 3));
        break;
    case Glyph::Stretch:
        // Une forme dont un seul cote est tire : c'est exactement ce que fait
        // la commande, et ce qui la distingue du deplacement.
        p.drawPolyline(QPolygonF({ { 3, 7 }, { 13, 7 }, { 13, 17 }, { 3, 17 }, { 3, 7 } }));
        p.drawLine(QPointF(13, 12), QPointF(21, 12));
        p.drawPolyline(QPolygonF({ { 18, 9 }, { 21, 12 }, { 18, 15 } }));
        break;
    case Glyph::Array:
        // La matrice de copies : deux lignes de trois.
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 3; ++column)
                p.drawRect(QRectF(3 + column * 7, 5 + row * 8, 5, 6));
        }
        break;
    case Glyph::Align:
        // Un bord de reference, et trois barres qui viennent s'y poser.
        p.drawLine(QPointF(4, 3), QPointF(4, 21));
        p.drawRect(QRectF(4, 5, 14, 4));
        p.drawRect(QRectF(4, 11, 9, 4));
        p.drawRect(QRectF(4, 17, 17, 4));
        break;
    case Glyph::Trim:
        // Un trait coupe net, et la limite qui l'a coupe.
        p.drawLine(QPointF(3, 16), QPointF(11, 16));
        p.drawLine(QPointF(15, 4), QPointF(15, 20));
        p.drawLine(QPointF(18, 8), QPointF(21, 8));
        break;
    case Glyph::LabelFolio:
        // Une etiquette qui porte un numero de page : le renvoi dit sur quel
        // folio le potentiel se poursuit.
        p.drawPolyline(QPolygonF({ { 3, 8 }, { 15, 8 }, { 20, 12 }, { 15, 16 }, { 3, 16 },
                                   { 3, 8 } }));
        p.drawLine(QPointF(8, 11), QPointF(8, 13.5));
        p.drawLine(QPointF(11.5, 11), QPointF(11.5, 13.5));
        break;
    case Glyph::SignalOut:
        // La fleche de source : le signal quitte le potentiel, la pointe sort
        // du trait.
        p.drawLine(QPointF(3, 12), QPointF(12, 12));
        p.drawPolygon(QPolygonF({ { 12, 7 }, { 21, 12 }, { 12, 17 } }));
        break;
    case Glyph::SignalIn:
        // La fleche de destination : le meme dessin retourne, la pointe
        // rejoint le trait. Source et destination doivent se distinguer d'un
        // coup d'oeil — c'est tout ce qui les separe sur un folio.
        p.drawLine(QPointF(21, 12), QPointF(12, 12));
        p.drawPolygon(QPolygonF({ { 12, 7 }, { 3, 12 }, { 12, 17 } }));
        break;
    case Glyph::Line:
        // Un segment et ses deux extremites : c'est ce qu'on pose, et c'est
        // ce qui le distingue du fil, qui conduit.
        p.drawLine(QPointF(5, 19), QPointF(19, 5));
        fill(stroke);
        p.drawEllipse(QPointF(5, 19), 1.7, 1.7);
        p.drawEllipse(QPointF(19, 5), 1.7, 1.7);
        strokeOnly();
        break;
    case Glyph::Rectangle:
        p.drawRect(QRectF(3.5, 6, 17, 12));
        break;
    case Glyph::Circle:
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        break;
    case Glyph::Arc:
        // Un demi-tour ouvert vers le bas, avec ses deux bouts : sans eux on
        // le confond avec le cercle a seize pixels.
        p.drawArc(QRectF(3, 6, 18, 18), 0, 180 * 16);
        fill(stroke);
        p.drawEllipse(QPointF(3, 15), 1.6, 1.6);
        p.drawEllipse(QPointF(21, 15), 1.6, 1.6);
        strokeOnly();
        break;
    case Glyph::Polyline:
        // Une ligne brisee et ses sommets : ce sont les sommets qui la
        // distinguent de la ligne simple.
        p.drawPolyline(QPolygonF({ { 3, 18 }, { 9, 8 }, { 15, 16 }, { 21, 5 } }));
        fill(stroke);
        for (const QPointF &v : { QPointF(3, 18), QPointF(9, 8), QPointF(15, 16),
                                  QPointF(21, 5) })
            p.drawEllipse(v, 1.5, 1.5);
        strokeOnly();
        break;
    case Glyph::MeasureLength:
        // Une ligne de cote : deux traits d'attache, une fleche a chaque bout.
        p.drawLine(QPointF(4, 4), QPointF(4, 20));
        p.drawLine(QPointF(20, 4), QPointF(20, 20));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.drawPolyline(QPolygonF({ { 7.5, 9 }, { 4, 12 }, { 7.5, 15 } }));
        p.drawPolyline(QPolygonF({ { 16.5, 9 }, { 20, 12 }, { 16.5, 15 } }));
        break;
    case Glyph::Join:
        // Deux traits qui se rejoignent, et le point de soudure.
        p.drawLine(QPointF(3, 12), QPointF(10.5, 12));
        p.drawLine(QPointF(13.5, 12), QPointF(21, 12));
        fill(stroke);
        p.drawEllipse(QPointF(12, 12), 2.4, 2.4);
        strokeOnly();
        break;
    case Glyph::Break:
        // Le meme trait, mais l'intervalle reste ouvert et les deux bords
        // sont marques : c'est exactement l'inverse de joindre.
        p.drawLine(QPointF(3, 12), QPointF(9, 12));
        p.drawLine(QPointF(15, 12), QPointF(21, 12));
        p.drawLine(QPointF(9, 7), QPointF(9, 17));
        p.drawLine(QPointF(15, 7), QPointF(15, 17));
        break;
    case Glyph::Extend:
        // Le meme trait, mais allonge jusqu'a la limite.
        p.drawLine(QPointF(3, 16), QPointF(15, 16));
        p.drawPolyline(QPolygonF({ { 12, 13 }, { 15, 16 }, { 12, 19 } }));
        p.drawLine(QPointF(19, 4), QPointF(19, 20));
        break;
    }

    p.end();
    pixmap.setDevicePixelRatio(kRatio);
    const QIcon result(pixmap);
    g_iconCache.insert(key, result);
    return result;
}

} // namespace dsn
