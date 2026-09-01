#include "theme.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace dsn {

namespace {

ThemeColors g_colors;
bool g_dark = true;
QHash<int, QIcon> g_iconCache;

ThemeColors darkColors()
{
    ThemeColors c;
    c.dark = true;
    // Neutres legerement bleu-vert : ils s'accordent au bleu des conducteurs
    // sans jamais entrer en concurrence avec la feuille blanche du dessin.
    c.window = QColor(0x16, 0x19, 0x1B);
    c.surface = QColor(0x1E, 0x23, 0x25);
    c.elevated = QColor(0x27, 0x2D, 0x30);
    c.border = QColor(0x33, 0x3A, 0x3D);
    c.borderStrong = QColor(0x46, 0x50, 0x54);
    c.text = QColor(0xE6, 0xEB, 0xE8);
    c.textMuted = QColor(0x8B, 0x97, 0x99);
    c.accent = QColor(0x4B, 0x9F, 0xE1);
    c.accentHover = QColor(0x69, 0xB2, 0xEB);
    c.accentText = QColor(0x0B, 0x14, 0x18);
    c.danger = QColor(0xE2, 0x68, 0x5C);
    c.success = QColor(0x7E, 0xBC, 0x55);
    c.warning = QColor(0xE0, 0xA3, 0x3C);
    return c;
}

ThemeColors lightColors()
{
    ThemeColors c;
    c.dark = false;
    c.window = QColor(0xF0, 0xF2, 0xF1);
    c.surface = QColor(0xFF, 0xFF, 0xFF);
    c.elevated = QColor(0xF7, 0xF9, 0xF8);
    c.border = QColor(0xDC, 0xE2, 0xE0);
    c.borderStrong = QColor(0xBD, 0xC7, 0xC4);
    c.text = QColor(0x17, 0x1C, 0x1A);
    c.textMuted = QColor(0x5F, 0x6B, 0x68);
    c.accent = QColor(0x0A, 0x5C, 0x9E);
    c.accentHover = QColor(0x0C, 0x6F, 0xBD);
    c.accentText = QColor(0xFF, 0xFF, 0xFF);
    c.danger = QColor(0xC0, 0x39, 0x2B);
    c.success = QColor(0x3E, 0x7D, 0x2E);
    c.warning = QColor(0xA9, 0x72, 0x14);
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

QString styleSheet(const ThemeColors &c)
{
    const int r = Theme::radius();
    return QStringLiteral(R"(
* { outline: none; }

QWidget {
    background: %WINDOW%;
    color: %TEXT%;
    font-size: 10pt;
}

QToolTip {
    background: %ELEVATED%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    padding: 5px 8px;
}

/* --- barre de menus ------------------------------------------------- */
QMenuBar {
    background: %WINDOW%;
    border-bottom: 1px solid %BORDER%;
    padding: 2px 6px;
}
QMenuBar::item {
    padding: 6px 11px;
    border-radius: 5px;
    background: transparent;
}
QMenuBar::item:selected { background: %HOVER%; }
QMenuBar::item:pressed  { background: %ACCENTSOFT%; color: %ACCENT%; }

QMenu {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: %R%px;
    padding: 6px;
}
QMenu::item {
    padding: 7px 26px 7px 30px;
    border-radius: 5px;
}
QMenu::item:selected  { background: %ACCENT%; color: %ACCENTTEXT%; }
QMenu::item:disabled  { color: %MUTED%; }
QMenu::separator      { height: 1px; background: %BORDER%; margin: 6px 8px; }
QMenu::icon           { padding-left: 10px; }

/* --- barres d'outils ------------------------------------------------ */
QToolBar {
    background: %WINDOW%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    padding: 5px 6px;
    spacing: 3px;
}
QToolBar::separator {
    width: 1px;
    background: %BORDER%;
    margin: 5px 7px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
    padding: 6px 9px;
    color: %TEXT%;
}
QToolButton:hover    { background: %HOVER%; }
QToolButton:pressed  { background: %ACCENTSOFT%; }
QToolButton:checked  {
    background: %ACCENTSOFT%;
    border-color: %ACCENTBORDER%;
    color: %ACCENT%;
}
QToolButton:disabled { color: %MUTED%; }

/* --- panneaux ancrables --------------------------------------------- */
QDockWidget {
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
    font-weight: 600;
}
QDockWidget::title {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-bottom: none;
    border-top-left-radius: %R%px;
    border-top-right-radius: %R%px;
    padding: 8px 10px;
    text-align: left;
}
QDockWidget > QWidget {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-top: none;
    border-bottom-left-radius: %R%px;
    border-bottom-right-radius: %R%px;
}
QDockWidget::close-button, QDockWidget::float-button {
    background: transparent;
    border: none;
    padding: 2px;
}
QDockWidget::close-button:hover, QDockWidget::float-button:hover {
    background: %HOVER%;
    border-radius: 4px;
}

QMainWindow::separator {
    background: %WINDOW%;
    width: 6px;
    height: 6px;
}
QMainWindow::separator:hover { background: %ACCENTSOFT%; }

/* --- champs de saisie ----------------------------------------------- */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QDateEdit, QComboBox {
    background: %FIELD%;
    border: 1px solid %BORDER%;
    border-radius: 5px;
    padding: 5px 8px;
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
}
QLineEdit:disabled, QComboBox:disabled { color: %MUTED%; background: %WINDOW%; }

QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: 5px;
    padding: 4px;
    selection-background-color: %ACCENT%;
    selection-color: %ACCENTTEXT%;
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    background: transparent;
    border: none;
    width: 16px;
}

/* --- boutons -------------------------------------------------------- */
QPushButton {
    background: %ELEVATED%;
    border: 1px solid %BORDER%;
    border-radius: 5px;
    padding: 7px 16px;
    min-height: 18px;
}
QPushButton:hover   { background: %HOVER%; border-color: %BORDERSTRONG%; }
QPushButton:pressed { background: %ACCENTSOFT%; }
QPushButton:default {
    background: %ACCENT%;
    border-color: %ACCENT%;
    color: %ACCENTTEXT%;
    font-weight: 600;
}
QPushButton:default:hover { background: %ACCENTHOVER%; border-color: %ACCENTHOVER%; }
QPushButton:disabled { color: %MUTED%; background: %WINDOW%; }

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

/* --- listes et tableaux --------------------------------------------- */
QListWidget, QListView, QTreeView, QTableWidget, QTableView {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 5px;
    alternate-background-color: %ELEVATED%;
    selection-background-color: %ACCENT%;
    selection-color: %ACCENTTEXT%;
    padding: 2px;
}
QListWidget::item, QListView::item {
    padding: 6px 8px;
    border-radius: 5px;
    margin: 1px 2px;
}
QListWidget::item:hover, QListView::item:hover { background: %HOVER%; }
QListWidget::item:selected, QListView::item:selected {
    background: %ACCENTSOFT%;
    color: %TEXT%;
    border: 1px solid %ACCENTBORDER%;
}
QTableView::item { padding: 5px 8px; }
QTableView::item:selected { background: %ACCENTSOFT%; color: %TEXT%; }

QHeaderView::section {
    background: %ELEVATED%;
    color: %MUTED%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    border-right: 1px solid %BORDER%;
    padding: 7px 9px;
    font-weight: 600;
}
QHeaderView::section:last { border-right: none; }
QTableCornerButton::section { background: %ELEVATED%; border: none; }

/* --- onglets -------------------------------------------------------- */
QTabWidget::pane { border: none; }
QTabBar::tab {
    background: transparent;
    color: %MUTED%;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 8px 14px;
    margin-right: 2px;
}
QTabBar::tab:hover    { color: %TEXT%; }
QTabBar::tab:selected { color: %ACCENT%; border-bottom-color: %ACCENT%; font-weight: 600; }

/* --- ascenseurs ----------------------------------------------------- */
QScrollBar:vertical   { background: transparent; width: 11px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 2px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: %SCROLL%;
    border-radius: 4px;
    min-height: 28px;
    min-width: 28px;
}
QScrollBar::handle:hover { background: %BORDERSTRONG%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* --- divers --------------------------------------------------------- */
QGroupBox {
    border: 1px solid %BORDER%;
    border-radius: %R%px;
    margin-top: 14px;
    padding: 12px 10px 10px 10px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 6px;
    color: %MUTED%;
}

QStatusBar {
    background: %WINDOW%;
    border-top: 1px solid %BORDER%;
    color: %MUTED%;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %MUTED%; padding: 2px 10px; }

/* Bascules d'aide au dessin, facon barre d'etat AutoCAD : eteintes elles
   s'effacent, allumees elles portent l'accent. */
QToolButton[statusToggle="true"] {
    color: %MUTED%;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 3px 9px;
    margin: 0 1px;
    font-size: 8.5pt;
    font-weight: 600;
    letter-spacing: 0.4px;
}
QToolButton[statusToggle="true"]:hover { background: %HOVER%; color: %TEXT%; }
QToolButton[statusToggle="true"]:checked {
    background: %ACCENTSOFT%;
    border-color: %ACCENTBORDER%;
    color: %ACCENT%;
}

QSplitter::handle { background: %WINDOW%; }
QSplitter::handle:hover { background: %ACCENTSOFT%; }

QLabel { background: transparent; }
QScrollArea { background: transparent; border: none; }
QDialog { background: %WINDOW%; }
)")
            .replace(QStringLiteral("%WINDOW%"), hex(c.window))
            .replace(QStringLiteral("%SURFACE%"), hex(c.surface))
            .replace(QStringLiteral("%ELEVATED%"), hex(c.elevated))
            .replace(QStringLiteral("%FIELD%"), hex(c.dark ? c.window : c.surface))
            .replace(QStringLiteral("%BORDERSTRONG%"), hex(c.borderStrong))
            .replace(QStringLiteral("%BORDER%"), hex(c.border))
            .replace(QStringLiteral("%TEXT%"), hex(c.text))
            .replace(QStringLiteral("%MUTED%"), hex(c.textMuted))
            .replace(QStringLiteral("%ACCENTHOVER%"), hex(c.accentHover))
            .replace(QStringLiteral("%ACCENTTEXT%"), hex(c.accentText))
            .replace(QStringLiteral("%ACCENTSOFT%"), rgba(c.accent, c.dark ? 46 : 30))
            .replace(QStringLiteral("%ACCENTBORDER%"), rgba(c.accent, 130))
            .replace(QStringLiteral("%ACCENT%"), hex(c.accent))
            .replace(QStringLiteral("%HOVER%"), rgba(c.text, c.dark ? 20 : 16))
            .replace(QStringLiteral("%SCROLL%"), rgba(c.text, c.dark ? 48 : 52))
            .replace(QStringLiteral("%R%"), QString::number(r));
}

} // namespace

const ThemeColors &Theme::colors() { return g_colors; }
bool Theme::isDark() { return g_dark; }

void Theme::apply(QApplication &app, bool dark)
{
    g_dark = dark;
    g_colors = dark ? darkColors() : lightColors();
    Icons::invalidate();

    // Fusion sert de base : c'est le seul style Qt dont le rendu ne depend pas
    // du bureau, donc le seul qui donne la meme interface sur les trois
    // systemes vises.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, g_colors.window);
    palette.setColor(QPalette::WindowText, g_colors.text);
    palette.setColor(QPalette::Base, dark ? g_colors.window : g_colors.surface);
    palette.setColor(QPalette::AlternateBase, g_colors.elevated);
    palette.setColor(QPalette::Text, g_colors.text);
    palette.setColor(QPalette::PlaceholderText, g_colors.textMuted);
    palette.setColor(QPalette::Button, g_colors.elevated);
    palette.setColor(QPalette::ButtonText, g_colors.text);
    palette.setColor(QPalette::Highlight, g_colors.accent);
    palette.setColor(QPalette::HighlightedText, g_colors.accentText);
    palette.setColor(QPalette::ToolTipBase, g_colors.elevated);
    palette.setColor(QPalette::ToolTipText, g_colors.text);
    palette.setColor(QPalette::Link, g_colors.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, g_colors.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, g_colors.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, g_colors.textMuted);
    app.setPalette(palette);

    app.setStyleSheet(styleSheet(g_colors));
}

// ==========================================================================
// Icones

void Icons::invalidate() { g_iconCache.clear(); }

QIcon Icons::appIcon()
{
    // La marque : un folio clair sur fond sombre, traverse par un fil bleu
    // coude avec sa jonction — le geste fondateur du logiciel.
    QIcon icon;
    for (const int size : { 16, 32, 48, 64, 128, 256 }) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        const double s = size / 24.0;
        p.scale(s, s);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1A, 0x20, 0x23));
        p.drawRoundedRect(QRectF(0.6, 0.6, 22.8, 22.8), 5.0, 5.0);

        p.setBrush(QColor(0xF2, 0xF4, 0xF3));
        p.drawRoundedRect(QRectF(4.0, 3.2, 16.0, 17.6), 1.4, 1.4);

        const QColor blue(0x0E, 0x64, 0xA8);
        QPen wire(blue);
        wire.setWidthF(1.8);
        wire.setCapStyle(Qt::RoundCap);
        wire.setJoinStyle(Qt::RoundJoin);
        p.setPen(wire);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({ { 7.0, 7.2 }, { 12.0, 7.2 }, { 12.0, 12.8 },
                                   { 17.0, 12.8 } }));
        p.drawLine(QPointF(12.0, 12.8), QPointF(12.0, 17.6));

        p.setPen(Qt::NoPen);
        p.setBrush(blue);
        p.drawEllipse(QPointF(12.0, 12.8), 1.6, 1.6);
        p.drawEllipse(QPointF(7.0, 7.2), 1.25, 1.25);
        p.drawEllipse(QPointF(17.0, 12.8), 1.25, 1.25);
        p.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QIcon Icons::icon(Glyph glyph, bool dark)
{
    return icon(glyph, dark ? QColor(0xE6, 0xEB, 0xE8) : QColor(0x17, 0x1C, 0x1A));
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
    case Glyph::ExportPdf:
    case Glyph::ExportDxf:
    case Glyph::ExportCsv:
        p.drawPolyline(QPolygonF({ { 6, 3 }, { 14, 3 }, { 19, 8 }, { 19, 21 }, { 6, 21 }, { 6, 3 } }));
        p.drawLine(QPointF(12, 10), QPointF(12, 18));
        p.drawPolyline(QPolygonF({ { 9, 15 }, { 12, 18 }, { 15, 15 } }));
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
    case Glyph::Theme:
        p.drawEllipse(QPointF(12, 12), 7.5, 7.5);
        fill(stroke);
        p.drawChord(QRectF(4.5, 4.5, 15, 15), 90 * 16, 180 * 16);
        strokeOnly();
        break;
    }

    p.end();
    pixmap.setDevicePixelRatio(kRatio);
    const QIcon result(pixmap);
    g_iconCache.insert(key, result);
    return result;
}

} // namespace dsn
