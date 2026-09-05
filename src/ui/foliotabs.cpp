#include "foliotabs.h"

#include "document.h"
#include "folionavigator.h"
#include "theme.h"

#include "core/folio.h"

#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace dsn {

namespace {
constexpr int kNumberColumn = 22;
constexpr int kTabPadding = 10;
} // namespace

// La bande d'onglets, peinte a la main.
//
// Un QTabBar ordinaire ne porte qu'un texte et une couleur ; un onglet de page
// en veut DEUX — le numero au troisieme niveau d'encre, le titre au premier,
// comme une case de cartouche. C'est le meme motif que la barre d'etat, que le
// bandeau de zone du ruban et que la vignette de folio : deux niveaux d'encre
// disent « voici le reperage, voici la valeur » sans une ligne de plus.
//
// L'onglet actif porte un FILET D'ACCENT de 2 px du cote du canevas — donc en
// haut — et rien d'autre. Meme motif que l'onglet de ruban actif et que la
// bascule d'etat en marche : trois endroits, une seule chose a apprendre.
class FolioTabBar : public QWidget
{
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString number;
        QString title;
        int x = 0;
        int width = 0;
    };

    explicit FolioTabBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(FolioTabs::kTabHeight);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setEntries(QList<Entry> entries, int current)
    {
        m_entries = std::move(entries);
        m_current = current;
        layoutTabs();
        update();
    }

    int currentIndex() const { return m_current; }
    const QList<Entry> &entries() const { return m_entries; }

Q_SIGNALS:
    void chosen(int index);

protected:
    void resizeEvent(QResizeEvent *) override { layoutTabs(); }

    void mousePressEvent(QMouseEvent *event) override
    {
        const int index = at(event->position().toPoint());
        if (index >= 0 && index != m_current)
            Q_EMIT chosen(index);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int hover = at(event->position().toPoint());
        if (hover == m_hover)
            return;
        m_hover = hover;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        const ThemeColors &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.fillRect(rect(), c.window);
        // Le filet du haut separe la bande du dessin : c'est le seul bord.
        p.fillRect(0, 0, width(), 1, c.border);

        const QFont numberFont = Theme::monoFont(8);
        const QFont titleFont = Theme::uiFont(9);
        const QFontMetricsF numberMetrics(numberFont);

        for (int i = 0; i < m_entries.size(); ++i) {
            const Entry &entry = m_entries.at(i);
            const QRect box(entry.x, 0, entry.width, height());
            const bool active = i == m_current;

            if (active) {
                p.fillRect(box, c.surface);
                // Le filet d'accent du cote du canevas.
                p.fillRect(box.x(), 0, box.width(), 2, c.accent);
            } else if (i == m_hover) {
                p.fillRect(box, QColor(c.text.red(), c.text.green(), c.text.blue(),
                                       c.dark ? 16 : 14));
            }
            // Un filet entre deux onglets, jamais un cadre.
            if (i > 0)
                p.fillRect(box.x(), 6, 1, height() - 12, c.border);

            const QRect numberBox(box.x() + kTabPadding, 0, kNumberColumn, height());
            p.setFont(numberFont);
            p.setPen(c.textFaint);
            p.drawText(numberBox, Qt::AlignLeft | Qt::AlignVCenter, entry.number);

            const QRect titleBox(numberBox.right() + 2, 0,
                                 box.right() - numberBox.right() - kTabPadding, height());
            p.setFont(titleFont);
            p.setPen(active ? c.text : c.textMuted);
            const QFontMetricsF titleMetrics(titleFont);
            p.drawText(titleBox, Qt::AlignLeft | Qt::AlignVCenter,
                       titleMetrics.elidedText(entry.title, Qt::ElideRight,
                                               titleBox.width()));
        }
    }

private:
    int at(const QPoint &point) const
    {
        for (int i = 0; i < m_entries.size(); ++i) {
            const Entry &entry = m_entries.at(i);
            if (point.x() >= entry.x && point.x() < entry.x + entry.width)
                return i;
        }
        return -1;
    }

    void layoutTabs()
    {
        // Chaque onglet demande la place de son titre, et tous se serrent
        // egalement quand le dossier deborde : une page qui disparait de la
        // bande n'est plus une page qu'on retrouve d'un coup d'oeil.
        const QFontMetricsF titleMetrics(Theme::uiFont(9));
        int total = 0;
        QList<int> wanted;
        for (const Entry &entry : std::as_const(m_entries)) {
            const int w = kTabPadding * 2 + kNumberColumn + 2
                    + int(titleMetrics.horizontalAdvance(entry.title)) + 4;
            wanted.append(std::clamp(w, 90, 220));
            total += wanted.last();
        }
        const double squeeze = total > width() && total > 0
                ? double(width()) / double(total)
                : 1.0;
        int x = 0;
        for (int i = 0; i < m_entries.size(); ++i) {
            m_entries[i].x = x;
            m_entries[i].width = std::max(48, int(wanted.at(i) * squeeze));
            x += m_entries.at(i).width;
        }
    }

    QList<Entry> m_entries;
    int m_current = -1;
    int m_hover = -1;
};

// --------------------------------------------------------------------------

FolioTabs::FolioTabs(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // La bande de vignettes vient AU-DESSUS des onglets : elle se deplie vers
    // le dessin, comme un tiroir qu'on tire depuis sa poignee.
    m_stripHost = new QWidget(this);
    m_stripHost->setFixedHeight(kStripHeight);
    auto *stripLayout = new QVBoxLayout(m_stripHost);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(0);
    m_strip = new FolioNavigator(m_document, m_stripHost);
    stripLayout->addWidget(m_strip);
    m_stripHost->hide();
    column->addWidget(m_stripHost);

    auto *row = new QWidget(this);
    row->setFixedHeight(kTabHeight);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, Theme::space(1), 0);
    rowLayout->setSpacing(0);

    m_tabs = new FolioTabBar(row);
    rowLayout->addWidget(m_tabs, 1);

    // Deux boutons, et pas un de plus : ajouter une page, et voir les
    // vignettes. Tout le reste — renommer, dupliquer, deplacer, supprimer —
    // vit dans le menu contextuel du navigateur, la ou il vivait deja.
    m_add = new QToolButton(row);
    m_add->setAutoRaise(true);
    m_add->setFocusPolicy(Qt::NoFocus);
    m_add->setProperty("folioTabButton", true);
    m_add->setToolTip(tr("Nouveau folio (NF)"));
    connect(m_add, &QToolButton::clicked, this, [this] { m_strip->addFolioFromCommand(); });
    rowLayout->addWidget(m_add);

    m_expand = new QToolButton(row);
    m_expand->setAutoRaise(true);
    m_expand->setCheckable(true);
    m_expand->setFocusPolicy(Qt::NoFocus);
    m_expand->setProperty("folioTabButton", true);
    connect(m_expand, &QToolButton::toggled, this, &FolioTabs::setStripVisible);
    rowLayout->addWidget(m_expand);

    column->addWidget(row);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    connect(m_tabs, &FolioTabBar::chosen, this, [this](int index) {
        const Folio *folio = m_document->project().folioAt(index);
        if (folio)
            Q_EMIT folioChosen(folio->id());
    });
    connect(m_document, &Document::folioListChanged, this, &FolioTabs::refresh);
    connect(m_document, &Document::currentFolioChanged, this, [this] { refresh(); });
    // ET a chaque commande. `folioListChanged` ne couvre pas tout : ajouter,
    // renommer ou deplacer un folio passe par une commande, et le navigateur
    // etait rafraichi A LA MAIN depuis cinq endroits de la fenetre — un
    // sixieme aurait fini par manquer. La barre relit a chaque poussee : elle
    // ne detient rien, donc relire ne coute qu'une poignee de chaines.
    connect(m_document, &Document::undoStateChanged, this, &FolioTabs::refresh);

    applyTheme();
    refresh();
}

void FolioTabs::refresh()
{
    if (m_updating)
        return;
    m_updating = true;

    QList<FolioTabBar::Entry> entries;
    for (int i = 0; i < m_document->folioCount(); ++i) {
        const Folio *folio = m_document->project().folioAt(i);
        if (!folio)
            continue;
        FolioTabBar::Entry entry;
        entry.id = folio->id();
        entry.number = folio->number;
        entry.title = folio->title.isEmpty() ? tr("Sans titre") : folio->title;
        entries.append(entry);
    }
    m_tabs->setEntries(entries, m_document->currentFolioIndex());

    m_updating = false;
}

int FolioTabs::tabCount() const { return int(m_tabs->entries().size()); }

QString FolioTabs::tabTitle(int index) const
{
    if (index < 0 || index >= m_tabs->entries().size())
        return {};
    return m_tabs->entries().at(index).title;
}

bool FolioTabs::stripVisible() const { return m_stripHost->isVisible(); }

void FolioTabs::setStripVisible(bool visible)
{
    if (m_stripHost->isVisible() == visible)
        return;
    m_stripHost->setVisible(visible);
    if (m_expand->isChecked() != visible) {
        m_expand->blockSignals(true);
        m_expand->setChecked(visible);
        m_expand->blockSignals(false);
    }
    applyTheme();
    updateGeometry();
    Q_EMIT stripVisibleChanged(visible);
}

void FolioTabs::applyTheme()
{
    const bool visible = m_stripHost->isVisible();
    m_add->setIcon(Icons::icon(Icons::Glyph::Plus));
    m_expand->setIcon(Icons::icon(visible ? Icons::Glyph::Down : Icons::Glyph::Up));
    m_expand->setToolTip(visible ? tr("Replier les vignettes de folios (Ctrl+4)")
                                 : tr("Déplier les vignettes de folios (Ctrl+4)"));
    m_tabs->update();
}

} // namespace dsn

#include "foliotabs.moc"
