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
    // Le papier, et son encre. Regle 6 : c'est le seul blanc pur du logiciel,
    // et il est le meme dans les deux themes — d'ou une seule palette de
    // dessin au lieu de deux.
    c.paper = QColor(0xFF, 0xFF, 0xFF);
    c.ink = QColor(0x15, 0x1A, 0x18);
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
    // Le vide reste plus sombre que le chrome, comme en sombre : c'est la
    // regle 1, et elle ne s'inverse pas d'un theme a l'autre.
    c.canvas = QColor(0xC8, 0xCF, 0xD4);
    c.window = QColor(0xE8, 0xEC, 0xEF);
    // Un panneau ne peut pas porter la couleur d'une feuille : sinon la
    // hierarchie que tout le reste du theme construit s'effondre au dernier
    // centimetre, la ou l'oeil travaille. Le theme clair n'etait pas le parent
    // pauvre par manque de reglages — c'etaient ces deux valeurs, qui
    // annulaient les regles 1 et 2.
    c.surface = QColor(0xF4, 0xF6, 0xF8);
    c.elevated = QColor(0xFD, 0xFD, 0xFE); // regle 6 : jamais #ffffff
    c.paper = QColor(0xFF, 0xFF, 0xFF);    // le meme qu'en sombre
    c.ink = QColor(0x15, 0x1A, 0x18);      // le meme qu'en sombre
    // Deux pour cent d'ecart de luminosite ne font pas une separation : en
    // clair, « des filets, pas des boites » devenait « ni filets ni boites ».
    c.border = QColor(0xCF, 0xD6, 0xDB);
    c.borderStrong = QColor(0xA8, 0xB3, 0xBA);
    c.text = QColor(0x10, 0x16, 0x19);
    c.textMuted = QColor(0x56, 0x64, 0x6C);
    c.textFaint = QColor(0x7F, 0x8C, 0x94);
    // En clair, l'accent doit tenir sur du blanc : il descend en luminosite
    // sans changer de teinte, sinon les deux themes n'ont pas la meme marque.
    c.accent = QColor(0x0B, 0x76, 0xB8);
    c.accentHover = QColor(0x0D, 0x8B, 0xD6);
    c.accentText = QColor(0xFF, 0xFF, 0xFF);
    c.danger = QColor(0xB2, 0x34, 0x28);
    c.success = QColor(0x2C, 0x6F, 0x33);
    c.warning = QColor(0x8F, 0x62, 0x10);
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

/* L'INVITE — la seule ligne qui dise ce que le logiciel attend maintenant.
   Elle porte l'accent : c'est exactement la reserve d'usage de la regle 3,
   puisque rien d'autre dans le bandeau ne designe ce qui est actif pendant
   un geste. Comme l'historique, c'est du texte pose : ni fond ni cadre. */
QLabel[commandPrompt="true"] {
    background: transparent;
    border: none;
    padding: 3px 9px 1px 9px;
    color: %ACCENT%;
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

/* --- barre d'etat ------------------------------------------------------
   Une bande de cartouche. Chaque mesure est une CASE : un libelle grave a
   gauche, la valeur en chasse fixe a sa droite, un filet de 1 px avant la
   suivante. Pas de rayon et pas d'aplat — un cartouche n'a ni coin arrondi
   ni fond colore, et la barre d'etat est le cartouche de la fenetre.

   La gauche reste libre : c'est la que le message temporaire s'affiche
   quand le bandeau de commande est replie, et il ne doit rien bousculer. */
QStatusBar {
    background: %WINDOW%;
    border-top: 1px solid %BORDER%;
    color: %MUTED%;
    padding: 0 4px;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %MUTED%; padding: 0; }

/* Le libelle est au troisieme niveau d'encre, en capitales gravees : il se
   lit quand on le cherche et disparait quand on lit la valeur. Les capitales
   et l'espacement viennent de Theme::engrave() — Qt n'a ni « text-transform »
   ni « letter-spacing » en feuille de style. */
QStatusBar QLabel[cellLabel="true"] {
    color: %FAINT%;
    padding: 0 0 0 9px;
}
QStatusBar QLabel[cellValue="true"] {
    color: %TEXT%;
    padding: 0 9px 0 5px;
}
QStatusBar QFrame[cellRule="true"] { color: %BORDER%; }

/* La case de revision, en CREUX : le plan `canvas` est le plus profond des
   quatre, et c'est ce que fait la case REV. d'un cartouche — un renfoncement
   dans la bande, pas une case de plus. Elle est peinte par la feuille de
   style et non par une QPalette : une palette est figee au moment ou on la
   pose et ne suivrait pas un changement de theme. Un QWidget nu n'accepte un
   fond de feuille de style qu'avec Qt::WA_StyledBackground. */
QStatusBar QWidget[revisionCell="true"] { background: %CANVAS%; }

/* Regle 3 tenue : la bascule eteinte est au troisieme niveau d'encre et ne
   porte AUCUN aplat. En marche elle prend le meme filet de 2 px que l'onglet
   de ruban actif — un seul motif a apprendre pour deux endroits. L'aplat
   permanent d'avant disait « actif » sans interruption : il ne renseignait
   plus, et il criait dans le coin le plus visible de la fenetre.

   Pas de font-weight ici : Qt calcule la taille du bouton dans son etat
   normal et le texte se rogne a l'etat coche. Piege deja paye trois fois
   dans ce depot ; la distinction se fait par le filet et par la couleur. */
QToolButton[statusToggle="true"] {
    color: %FAINT%;
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    padding: 3px 8px 1px 8px;
    margin: 0;
    font-size: 8pt;
}
QToolButton[statusToggle="true"]:hover {
    background: transparent;
    color: %TEXT%;
}
QToolButton[statusToggle="true"]:checked {
    background: transparent;
    color: %TEXT%;
    border-bottom: 2px solid %ACCENT%;
}
QToolButton[statusToggle="true"]:checked:hover {
    color: %TEXT%;
    border-bottom: 2px solid %ACCENTHOVER%;
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
            .replace(QStringLiteral("%CANVAS%"), hex(c.canvas))
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
    case Glyph::SwapSymbol:
        // Deux boitiers et deux fleches croisees : l'un prend la place de
        // l'autre. C'est l'echange, pas la copie — d'ou les deux sens.
        p.drawRect(QRectF(3, 3, 7, 7));
        p.drawRect(QRectF(14, 14, 7, 7));
        p.drawLine(QPointF(11, 6.5), QPointF(17.5, 6.5));
        p.drawPolyline(QPolygonF({ { 15, 4.5 }, { 17.5, 6.5 }, { 15, 8.5 } }));
        p.drawLine(QPointF(13, 17.5), QPointF(6.5, 17.5));
        p.drawPolyline(QPolygonF({ { 9, 15.5 }, { 6.5, 17.5 }, { 9, 19.5 } }));
        break;
    case Glyph::Dimension:
        // Une cote alignee : la ligne inclinee, ses deux fleches et les deux
        // lignes d'attache. C'est le dessin d'une cote, pas une regle.
        p.drawLine(QPointF(4, 19), QPointF(8, 7));
        p.drawLine(QPointF(16, 21), QPointF(20, 9));
        p.drawLine(QPointF(6.5, 12), QPointF(18.5, 14));
        p.drawPolyline(QPolygonF({ { 9, 10.5 }, { 6.5, 12 }, { 9, 13.5 } }));
        p.drawPolyline(QPolygonF({ { 16, 12.5 }, { 18.5, 14 }, { 16, 15.5 } }));
        break;
    case Glyph::DimensionH:
        // La meme, a plat : deux attaches verticales et la ligne entre elles.
        p.drawLine(QPointF(4, 6), QPointF(4, 18));
        p.drawLine(QPointF(20, 6), QPointF(20, 18));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.drawPolyline(QPolygonF({ { 7, 9.5 }, { 4, 12 }, { 7, 14.5 } }));
        p.drawPolyline(QPolygonF({ { 17, 9.5 }, { 20, 12 }, { 17, 14.5 } }));
        break;
    case Glyph::DimensionV:
        // Et debout. Les trois se distinguent d'un coup d'oeil : c'est la
        // regle du ruban, deux commandes voisines ne partagent pas un dessin.
        p.drawLine(QPointF(6, 4), QPointF(18, 4));
        p.drawLine(QPointF(6, 20), QPointF(18, 20));
        p.drawLine(QPointF(12, 4), QPointF(12, 20));
        p.drawPolyline(QPolygonF({ { 9.5, 7 }, { 12, 4 }, { 14.5, 7 } }));
        p.drawPolyline(QPolygonF({ { 9.5, 17 }, { 12, 20 }, { 14.5, 17 } }));
        break;
    case Glyph::Ortho:
        // L'angle droit : c'est tout ce que dit ORTHO.
        p.drawPolyline(QPolygonF({ { 5, 4 }, { 5, 19 }, { 20, 19 } }));
        p.drawRect(QRectF(5, 15, 4, 4));
        break;
    case Glyph::Polar:
        // Un angle et son arc : le repérage polaire contraint la direction.
        p.drawLine(QPointF(4, 20), QPointF(20, 20));
        p.drawLine(QPointF(4, 20), QPointF(18, 7));
        p.drawArc(QRectF(-4, 12, 16, 16), 0, 45 * 16);
        break;
    case Glyph::Surfer:
        // Deux planches et le saut de l'une a l'autre : le Surfer suit un
        // element a travers le dossier.
        p.drawRect(QRectF(3, 5, 7, 9));
        p.drawRect(QRectF(14, 11, 7, 9));
        p.drawLine(QPointF(10, 8), QPointF(17, 8));
        p.drawPolyline(QPolygonF({ { 14.5, 5.5 }, { 17.5, 8 }, { 14.5, 10.5 } }));
        break;
    case Glyph::SelectAll:
        // Le cadre en pointille de « tout prendre », et ce qu'il contient.
        pen.setStyle(Qt::DotLine);
        p.setPen(pen);
        p.drawRect(QRectF(3, 3, 18, 18));
        pen.setStyle(Qt::SolidLine);
        p.setPen(pen);
        p.drawRect(QRectF(7, 7, 4, 4));
        p.drawRect(QRectF(13, 13, 4, 4));
        break;
    case Glyph::Move:
        // Les quatre fleches : deplacer, dans tous les sens.
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        p.drawPolyline(QPolygonF({ { 9.5, 5.5 }, { 12, 3 }, { 14.5, 5.5 } }));
        p.drawPolyline(QPolygonF({ { 9.5, 18.5 }, { 12, 21 }, { 14.5, 18.5 } }));
        p.drawPolyline(QPolygonF({ { 5.5, 9.5 }, { 3, 12 }, { 5.5, 14.5 } }));
        p.drawPolyline(QPolygonF({ { 18.5, 9.5 }, { 21, 12 }, { 18.5, 14.5 } }));
        break;
    case Glyph::Pan:
        // La main qui pousse la feuille : quatre doigts et le pouce. Le
        // panoramique deplace la VUE, pas le dessin — d'ou la main et non les
        // fleches de Deplacer.
        p.drawPolyline(QPolygonF({ { 7, 13 }, { 7, 8 }, { 9, 8 }, { 9, 12 } }));
        p.drawPolyline(QPolygonF({ { 11, 12 }, { 11, 6 }, { 13, 6 }, { 13, 12 } }));
        p.drawPolyline(QPolygonF({ { 15, 12 }, { 15, 8 }, { 17, 8 }, { 17, 14 } }));
        p.drawPolyline(QPolygonF({ { 7, 13 }, { 5, 15 }, { 8, 20 }, { 15, 20 }, { 17, 14 } }));
        break;
    case Glyph::Scoot:
        // Un appareil qui coulisse le long de son fil : le trait porte, la
        // boite glisse dessus.
        p.drawLine(QPointF(2, 12), QPointF(22, 12));
        p.drawRect(QRectF(9, 8, 6, 8));
        p.drawPolyline(QPolygonF({ { 5.5, 20 }, { 3, 17.5 }, { 5.5, 15 } }));
        p.drawPolyline(QPolygonF({ { 18.5, 20 }, { 21, 17.5 }, { 18.5, 15 } }));
        break;
    case Glyph::DraftingSettings:
        // Le compas de dessin, ouvert : les parametres du TRACE, pas ceux du
        // logiciel.
        p.drawLine(QPointF(12, 4), QPointF(6, 20));
        p.drawLine(QPointF(12, 4), QPointF(18, 20));
        p.drawArc(QRectF(4, 12, 16, 12), 200 * 16, 140 * 16);
        fill(stroke);
        p.drawEllipse(QPointF(12, 4), 1.6, 1.6);
        strokeOnly();
        break;
    case Glyph::EditComponent:
        // Un appareil et le crayon qui le regle.
        p.drawRect(QRectF(3, 7, 10, 10));
        p.drawLine(QPointF(3, 12), QPointF(1.5, 12));
        p.drawPolyline(QPolygonF({ { 13, 20 }, { 13, 17 }, { 20, 10 }, { 23, 13 },
                                   { 16, 20 }, { 13, 20 } }));
        break;
    case Glyph::PinNumbers:
        // Une broche et le chiffre qu'elle porte.
        p.drawLine(QPointF(3, 16), QPointF(11, 16));
        fill(stroke);
        p.drawEllipse(QPointF(11, 16), 1.8, 1.8);
        strokeOnly();
        p.drawPolyline(QPolygonF({ { 15, 6 }, { 17, 4 }, { 17, 14 } }));
        p.drawLine(QPointF(15, 14), QPointF(19.5, 14));
        break;
    case Glyph::Plc:
        // Une carte d'automate : le corps et ses voies, en rangee.
        p.drawRect(QRectF(6, 3, 12, 18));
        for (int i = 0; i < 4; ++i) {
            const double y = 6.0 + i * 4.0;
            p.drawLine(QPointF(2.5, y), QPointF(6, y));
            p.drawLine(QPointF(18, y), QPointF(21.5, y));
        }
        break;
    case Glyph::UnconnectedPins:
        // Une broche libre : le trait, et la croix qui dit qu'il ne va nulle
        // part.
        p.drawLine(QPointF(3, 12), QPointF(13, 12));
        p.drawLine(QPointF(16, 9), QPointF(22, 15));
        p.drawLine(QPointF(22, 9), QPointF(16, 15));
        break;
    case Glyph::Audit:
        // La planchette de controle et sa coche : l'audit passe le dossier en
        // revue, il ne verifie pas une case.
        p.drawRect(QRectF(4, 4, 16, 18));
        p.drawLine(QPointF(9, 4), QPointF(15, 4));
        p.drawPolyline(QPolygonF({ { 7.5, 13 }, { 10.5, 16 }, { 16.5, 9 } }));
        break;
    case Glyph::Quit:
        // La porte et la fleche qui en sort.
        p.drawPolyline(QPolygonF({ { 13, 3 }, { 4, 3 }, { 4, 21 }, { 13, 21 } }));
        p.drawLine(QPointF(9, 12), QPointF(21, 12));
        p.drawPolyline(QPolygonF({ { 17.5, 8.5 }, { 21, 12 }, { 17.5, 15.5 } }));
        break;
    case Glyph::Ladder:
        // L'echelle de commande : deux montants et ses barreaux.
        p.drawLine(QPointF(6, 3), QPointF(6, 21));
        p.drawLine(QPointF(18, 3), QPointF(18, 21));
        p.drawLine(QPointF(6, 8), QPointF(18, 8));
        p.drawLine(QPointF(6, 13), QPointF(18, 13));
        p.drawLine(QPointF(6, 18), QPointF(18, 18));
        break;
    case Glyph::ZoomPrevious:
        // La loupe et la fleche de retour : la vue precedente.
        p.drawEllipse(QPointF(11, 11), 6.0, 6.0);
        p.drawLine(QPointF(15.5, 15.5), QPointF(21, 21));
        p.drawPolyline(QPolygonF({ { 11, 8 }, { 8, 11 }, { 11, 14 } }));
        p.drawLine(QPointF(8, 11), QPointF(14.5, 11));
        break;
    case Glyph::DuplicateEdit:
        // Deux exemplaires et le crayon : dupliquer PUIS modifier.
        p.drawRect(QRectF(3, 3, 10, 10));
        p.drawRect(QRectF(7, 8, 10, 10));
        p.drawPolyline(QPolygonF({ { 14, 21 }, { 14, 18.5 }, { 20, 12.5 },
                                   { 22.5, 15 }, { 16.5, 21 }, { 14, 21 } }));
        break;
    case Glyph::Terminals:
        // Un bornier : le rail et ses bornes vissees, cote a cote.
        p.drawLine(QPointF(2, 17), QPointF(22, 17));
        for (int i = 0; i < 3; ++i) {
            const double x = 4.5 + i * 6.0;
            p.drawRect(QRectF(x, 6, 5, 11));
            p.drawLine(QPointF(x + 2.5, 3), QPointF(x + 2.5, 6));
        }
        break;
    case Glyph::CommandLine:
        // L'invite d'une ligne de commande : le chevron et le curseur.
        p.drawRect(QRectF(2.5, 5, 19, 14));
        p.drawPolyline(QPolygonF({ { 6, 9.5 }, { 9, 12 }, { 6, 14.5 } }));
        p.drawLine(QPointF(11, 15), QPointF(17, 15));
        break;
    case Glyph::CommandPalette:
        // Une liste et la loupe qui la cherche : la palette de commandes.
        p.drawLine(QPointF(3, 6), QPointF(14, 6));
        p.drawLine(QPointF(3, 11), QPointF(11, 11));
        p.drawLine(QPointF(3, 16), QPointF(9, 16));
        p.drawEllipse(QPointF(16, 15), 4.5, 4.5);
        p.drawLine(QPointF(19.5, 18.5), QPointF(22.5, 21.5));
        break;
    case Glyph::MatchProps:
        // Le pinceau qui reprend les proprietes d'un element pour les poser
        // sur un autre.
        p.drawRect(QRectF(6, 3, 12, 5));
        p.drawPolyline(QPolygonF({ { 9, 8 }, { 9, 13 }, { 15, 13 }, { 15, 8 } }));
        p.drawLine(QPointF(12, 13), QPointF(12, 21));
        break;
    case Glyph::StartPage:
        // Le toit et la porte : l'ecran d'accueil, la ou l'on revient.
        p.drawPolyline(QPolygonF({ { 3, 12 }, { 12, 4 }, { 21, 12 } }));
        p.drawPolyline(QPolygonF({ { 5.5, 10 }, { 5.5, 21 }, { 18.5, 21 }, { 18.5, 10 } }));
        p.drawRect(QRectF(10, 15, 4, 6));
        break;
    case Glyph::ProjectInfo:
        // La fiche du dossier : un feuillet et ses lignes de renseignement.
        p.drawRect(QRectF(4, 3, 16, 18));
        p.drawLine(QPointF(7, 8), QPointF(17, 8));
        p.drawLine(QPointF(7, 12), QPointF(17, 12));
        p.drawLine(QPointF(7, 16), QPointF(13, 16));
        break;
    case Glyph::PageSetup:
        // Une feuille et ses marges : la mise en page decide du format et du
        // cadre, pas du contenu.
        p.drawRect(QRectF(3, 4, 18, 16));
        pen.setStyle(Qt::DotLine);
        p.setPen(pen);
        p.drawRect(QRectF(6, 7, 12, 10));
        pen.setStyle(Qt::SolidLine);
        p.setPen(pen);
        break;
    case Glyph::ObjectSnap:
        // Le carre d'extremite d'AutoSnap, pose sur le bout d'un trait :
        // c'est la forme meme de l'accrochage aux objets.
        p.drawLine(QPointF(3, 19), QPointF(16, 6));
        p.drawRect(QRectF(13, 3, 6, 6));
        break;
    case Glyph::TextH1:
    case Glyph::TextH2:
    case Glyph::TextH3:
    case Glyph::TextH4: {
        // Quatre tailles de capitale, et la fleche de hauteur a cote. Le
        // dessin dit LA TAILLE : quatre entrees de menu qui partagent une
        // icone ne se distinguent qu'a la lecture du chiffre, alors que c'est
        // precisement la taille qu'on choisit.
        const double h = 7.0 + 3.5 * double(int(glyph) - int(Glyph::TextH1));
        const double base = 20.0;
        const double demi = h * 0.34;
        p.drawPolyline(QPolygonF({ { 8 - demi, base }, { 8, base - h }, { 8 + demi, base } }));
        p.drawLine(QPointF(8 - demi * 0.55, base - h * 0.35),
                   QPointF(8 + demi * 0.55, base - h * 0.35));
        p.drawLine(QPointF(18, base), QPointF(18, base - h));
        p.drawPolyline(QPolygonF({ { 16.5, base - h + 1.6 }, { 18, base - h },
                                   { 19.5, base - h + 1.6 } }));
        p.drawPolyline(QPolygonF({ { 16.5, base - 1.6 }, { 18, base }, { 19.5, base - 1.6 } }));
        break;
    }
    case Glyph::TitleBlock:
        // Une feuille et son cartouche, en bas a droite : c'est a cette
        // silhouette qu'on reconnait une planche avant meme de la lire.
        p.drawRect(QRectF(3, 3, 18, 18));
        p.drawRect(QRectF(11, 14, 10, 7));
        p.drawLine(QPointF(11, 17.5), QPointF(21, 17.5));
        p.drawLine(QPointF(16, 14), QPointF(16, 21));
        break;
    case Glyph::Find:
        // La loupe, et son manche. C'est le seul dessin qu'on reconnaisse
        // sans le lire.
        p.drawEllipse(QPointF(10, 10), 6.0, 6.0);
        p.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
        break;
    case Glyph::Expand:
        // Le miroir du precedent : chevron vers le dessin, montant du cote du
        // bord. C'est l'onglet du rail — le panneau revient de la ou il est
        // parti, et la fleche le dit.
        p.drawPolyline(QPolygonF({ { 10, 5 }, { 17, 12 }, { 10, 19 } }));
        p.drawLine(QPointF(6, 4), QPointF(6, 20));
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
    // Les quatre styles de trait. Ils se distinguent uniquement par leur
    // motif — c'est le sujet — et occupent la meme place : cote a cote dans
    // un menu, l'oeil compare les motifs et rien d'autre.
    case Glyph::StrokeSolid:
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        strokeOnly();
        break;
    case Glyph::StrokeDashed:
        p.drawLine(QPointF(3, 12), QPointF(8, 12));
        p.drawLine(QPointF(11, 12), QPointF(16, 12));
        p.drawLine(QPointF(19, 12), QPointF(21, 12));
        strokeOnly();
        break;
    case Glyph::StrokeDotted:
        fill(stroke);
        for (double x = 3.5; x <= 21.0; x += 3.5)
            p.drawEllipse(QPointF(x, 12), 1.0, 1.0);
        strokeOnly();
        break;
    case Glyph::StrokeDashDot:
        p.drawLine(QPointF(3, 12), QPointF(11, 12));
        fill(stroke);
        p.drawEllipse(QPointF(14, 12), 1.0, 1.0);
        strokeOnly();
        p.drawLine(QPointF(17, 12), QPointF(21, 12));
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
