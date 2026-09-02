#include "dockrail.h"

#include "theme.h"

#include <QAbstractButton>
#include <QDockWidget>
#include <QPainter>
#include <QVBoxLayout>

namespace dsn {

namespace {
// Assez large pour une icone de quatorze et ses marges, assez etroit pour
// qu'on le laisse en place : le rail vide ne doit pas couter de dessin.
constexpr int kRailWidth = 24;
constexpr int kGlyphSize = 14;
} // namespace

// --------------------------------------------------------------------------

// L'onglet d'un panneau tasse : un chevron et le nom grave a la verticale.
//
// Le nom compte autant que le chevron. Deux panneaux se tassent dans la meme
// colonne ; deux chevrons identiques l'un sous l'autre ne disent pas lequel
// ramene la palette de symboles — c'est la meme faute que deux commandes du
// ruban partageant une icone.
class RailTab : public QAbstractButton
{
public:
    explicit RailTab(const QString &title, QWidget *parent = nullptr)
        : QAbstractButton(parent), m_title(title.toUpper())
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QString title() const { return m_title; }

    QSize sizeHint() const override
    {
        const QFontMetrics metrics(Theme::engravedFont());
        return QSize(kRailWidth,
                     kGlyphSize + Theme::space(3) + metrics.horizontalAdvance(m_title)
                             + Theme::space(2));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const ThemeColors &colors = Theme::colors();
        const bool actif = underMouse() || isDown();
        if (actif)
            painter.fillRect(rect(), colors.elevated);

        // Encre pleine, pas l'encre d'accompagnement : l'onglet n'apparait que
        // lorsqu'un panneau est tasse, et il est alors la seule chose qui dise
        // comment le ramener. Ce qui doit se trouver ne se grave pas en gris.
        const QColor encre = actif ? colors.accent : colors.text;
        const QRect zoneGlyphe((width() - kGlyphSize) / 2, Theme::space(1), kGlyphSize,
                               kGlyphSize);
        Icons::icon(Icons::Glyph::Expand, encre).paint(&painter, zoneGlyphe);

        // Le nom court du haut vers le bas : c'est le sens de lecture d'un
        // onglet vertical, et celui que Qt donne a une rotation de 90 degres.
        painter.save();
        painter.setFont(Theme::engravedFont());
        painter.setPen(encre);
        painter.translate(width(), zoneGlyphe.bottom() + Theme::space(2));
        painter.rotate(90.0);
        // Apres la rotation, un point local (x, y) se lit a l'ecran en
        // (largeur - y, origine + x) : la ligne de base se choisit donc en y,
        // et c'est elle qui centre le texte dans la largeur du rail.
        const QFontMetrics metrics(Theme::engravedFont());
        painter.drawText(QPoint(0, width() / 2 + (metrics.ascent() - metrics.descent()) / 2),
                         m_title);
        painter.restore();
    }

private:
    QString m_title;
};

// --------------------------------------------------------------------------

DockRail::DockRail(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("dockRail"));
    setFixedWidth(kRailWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, Theme::space(1), 0, 0);
    m_layout->setSpacing(Theme::space(1));
    m_layout->addStretch(1);

    // Le rail part invisible : tous les panneaux sont ouverts au lancement.
    hide();
}

void DockRail::paintEvent(QPaintEvent *)
{
    // Le fond des panneaux et un filet du cote du dessin : le rail appartient
    // au chrome, pas au canevas. Un filet, pas un cadre — regle 2 du theme.
    QPainter painter(this);
    painter.fillRect(rect(), Theme::colors().surface);
    painter.fillRect(width() - 1, 0, 1, height(), Theme::colors().border);
}

void DockRail::watch(QDockWidget *dock, const QString &title, const QString &hint)
{
    if (!dock)
        return;

    auto *tab = new RailTab(title, this);
    tab->setToolTip(hint);
    connect(tab, &QAbstractButton::clicked, this,
            [this, dock] { Q_EMIT openRequested(dock); });
    tab->hide();
    m_layout->insertWidget(m_layout->count() - 1, tab);

    m_entries.append({ dock, tab });

    // Le rail suit le panneau quel que soit le chemin qui l'a cache : le
    // chevron de sa barre de titre, la commande d'affichage, ou la
    // restauration d'une disposition enregistree.
    connect(dock, &QDockWidget::visibilityChanged, this, [this] { refresh(); });
    refresh();
}

void DockRail::refresh()
{
    int tasses = 0;
    for (const Entry &entry : std::as_const(m_entries)) {
        // isHidden(), pas isVisible() : un panneau dont la fenetre n'est pas
        // encore affichee n'est pas tasse — il n'est pas encore montre.
        const bool tasse = entry.dock->isHidden();
        entry.tab->setVisible(tasse);
        if (tasse)
            ++tasses;
    }
    setVisible(tasses > 0);
}

QStringList DockRail::tabs() const
{
    QStringList noms;
    for (const Entry &entry : m_entries) {
        if (entry.dock->isHidden())
            noms.append(entry.tab->title());
    }
    return noms;
}

void DockRail::applyTheme()
{
    for (const Entry &entry : std::as_const(m_entries)) {
        entry.tab->updateGeometry();
        entry.tab->update();
    }
    update();
}

} // namespace dsn
