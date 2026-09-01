#include "commandpalette.h"

#include "theme.h"

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

namespace dsn {

namespace {

// Comparaison sans accents ni casse : personne ne doit avoir a taper
// « Repérage » avec son accent pour trouver la commande.
QString fold(const QString &text)
{
    QString out = text.normalized(QString::NormalizationForm_D).toLower();
    out.removeIf([](QChar c) { return c.category() == QChar::Mark_NonSpacing; });
    return out;
}

} // namespace

bool CommandPalette::matches(const QString &needle, const QString &haystack)
{
    if (needle.isEmpty())
        return true;
    const QString n = fold(needle);
    const QString h = fold(haystack);

    int at = 0;
    int first = -1;
    int last = -1;
    int letters = 0;
    for (const QChar c : n) {
        if (c.isSpace())
            continue;
        at = h.indexOf(c, at);
        if (at < 0)
            return false;
        if (first < 0)
            first = at;
        last = at;
        ++letters;
        ++at;
    }
    if (letters == 0)
        return true;

    // Les lettres doivent rester groupees. Sans cette regle, « bor » remonte
    // « Basculer le mode ortho » : les trois lettres y sont, dispersees sur
    // vingt caracteres, et la liste se remplit de bruit.
    return last - first <= letters * 3 + 4;
}

int CommandPalette::score(const QString &needle, const Entry &entry)
{
    if (needle.trimmed().isEmpty())
        return 100;

    const QString n = fold(needle.trimmed());
    const QString title = fold(entry.title);

    // Un titre qui commence par ce qu'on tape est ce qu'on cherchait : il
    // passe devant tout le reste.
    if (title.startsWith(n))
        return 0;
    // Un alias exact ensuite : qui tape « DC » veut DECALER, pas une commande
    // dont le libelle contient ces deux lettres.
    for (const QString &keyword : entry.keywords) {
        if (fold(keyword) == n)
            return 1;
    }
    if (title.contains(n))
        return 2;
    for (const QString &keyword : entry.keywords) {
        if (fold(keyword).startsWith(n))
            return 3;
    }
    if (matches(needle, entry.title))
        return 4;
    if (matches(needle, entry.detail) || matches(needle, entry.group))
        return 5;
    for (const QString &keyword : entry.keywords) {
        if (matches(needle, keyword))
            return 6;
    }
    return -1;
}

namespace {

// Roles ou vivent les morceaux d'une ligne. Le libelle complet ne suffit pas :
// le titre, le menu d'origine, le raccourci et la phrase n'ont ni la meme
// couleur ni la meme place.
constexpr int kGroupRole = Qt::UserRole + 1;
constexpr int kShortcutRole = Qt::UserRole + 2;
constexpr int kDetailRole = Qt::UserRole + 3;

// Le trace d'une ligne de la palette. Trois niveaux de lecture : le titre, le
// menu ou la commande se trouve, et ce qu'elle fait. Le raccourci est dessine
// en touche, a droite — c'est la facon de l'apprendre sans le chercher.
class PaletteDelegate : public QAbstractItemDelegate
{
public:
    using QAbstractItemDelegate::QAbstractItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        return QSize(320, index.data(kDetailRole).toString().isEmpty() ? 34 : 46);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const ThemeColors &c = Theme::colors();
        const bool enabled = index.flags().testFlag(Qt::ItemIsEnabled);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hover = option.state.testFlag(QStyle::State_MouseOver);

        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF row = QRectF(option.rect).adjusted(4, 1, -4, -1);
        if (selected || hover) {
            QColor fill = selected ? c.accent : c.text;
            fill.setAlpha(selected ? 44 : 16);
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(row, 6, 6);
        }
        if (selected) {
            // Une barre d'accent a gauche : elle designe la ligne meme quand
            // l'aplat est trop dilue pour se voir sur un ecran mal regle.
            p->setBrush(c.accent);
            p->drawRoundedRect(QRectF(row.left() + 2, row.top() + 6, 3, row.height() - 12),
                               1.5, 1.5);
        }

        const QString title = index.data(Qt::DisplayRole).toString();
        const QString group = index.data(kGroupRole).toString();
        const QString shortcut = index.data(kShortcutRole).toString();
        const QString detail = index.data(kDetailRole).toString();
        const bool twoLines = !detail.isEmpty();

        // La touche, a droite : encadree, a chasse fixe, pour qu'elle se lise
        // comme une touche et non comme du texte.
        double right = row.right() - 10;
        if (!shortcut.isEmpty()) {
            p->setFont(Theme::monoFont(8.5));
            const int w = p->fontMetrics().horizontalAdvance(shortcut) + 14;
            const QRectF cap(right - w, row.center().y() - 10, w, 20);
            p->setPen(QPen(c.border, 1.0));
            p->setBrush(c.dark ? c.canvas : c.window);
            p->drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            p->setPen(enabled ? c.textMuted : c.textFaint);
            p->drawText(cap, Qt::AlignCenter, shortcut);
            right = cap.left() - 12;
        }

        const double left = row.left() + 14;
        const double width = std::max(40.0, right - left);
        const double titleTop = twoLines ? row.top() + 5 : row.top() + 7;

        p->setFont(Theme::uiFont(10, int(QFont::DemiBold)));
        p->setPen(enabled ? (selected ? c.accent : c.text) : c.textFaint);
        const int titleWidth = p->fontMetrics().horizontalAdvance(title);
        p->drawText(QRectF(left, titleTop, width, 20), Qt::AlignLeft | Qt::AlignVCenter, title);

        if (!group.isEmpty()) {
            p->setFont(Theme::uiFont(9));
            p->setPen(c.textFaint);
            const double x = left + titleWidth + 10;
            if (x < right - 20) {
                p->drawText(QRectF(x, titleTop, right - x, 20),
                            Qt::AlignLeft | Qt::AlignVCenter, group);
            }
        }

        if (twoLines) {
            p->setFont(Theme::uiFont(9));
            p->setPen(c.textFaint);
            const QRectF line(left, row.top() + 24, width, 18);
            p->drawText(line, Qt::AlignLeft | Qt::AlignVCenter,
                        p->fontMetrics().elidedText(detail, Qt::ElideRight, int(line.width())));
        }
    }
};

} // namespace

CommandPalette::CommandPalette(QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("commandPalette"));
    resize(680, 440);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("paletteSearch"));
    m_search->setPlaceholderText(tr("Que voulez-vous faire ?"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("paletteList"));
    m_list->setUniformItemSizes(false);
    m_list->setItemDelegate(new PaletteDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list, 1);

    auto *hint = new QLabel(tr("↑ ↓ pour choisir · Entrée pour exécuter · Échap pour fermer"),
                            this);
    hint->setObjectName(QStringLiteral("paletteHint"));
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    // La palette s'habille a la main : c'est la seule fenetre du logiciel qui
    // flotte au-dessus du dessin, elle doit se detacher nettement.
    const ThemeColors &c = Theme::colors();
    setStyleSheet(QStringLiteral(
                          "#commandPalette { background: %1; border: 1px solid %2; "
                          "  border-radius: 10px; }"
                          "#paletteSearch { background: transparent; border: none; "
                          "  border-bottom: 1px solid %2; padding: 14px 16px; font-size: 15px; "
                          "  color: %3; }"
                          "#paletteList { background: transparent; border: none; "
                          "  padding: 6px; }"
                          "#paletteHint { color: %4; padding: 9px; border-top: 1px solid %2; "
                          "  font-size: 11px; }")
                          .arg(c.elevated.name(), c.border.name(), c.text.name(),
                               c.textFaint.name()));

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::refresh);
    connect(m_list, &QListWidget::itemActivated, this, [this] { runCurrent(); });
    m_search->installEventFilter(this);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_search || event->type() != QEvent::KeyPress)
        return QDialog::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    switch (key->key()) {
    case Qt::Key_Down:
    case Qt::Key_Up:
    case Qt::Key_PageDown:
    case Qt::Key_PageUp:
        // Les fleches naviguent la liste sans quitter le champ : on tape et on
        // choisit d'une seule main.
        QApplication::sendEvent(m_list, key);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        runCurrent();
        return true;
    default:
        break;
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::setEntries(QVector<Entry> entries)
{
    m_entries = std::move(entries);
}

void CommandPalette::open()
{
    m_search->clear();
    refresh();
    if (QWidget *anchor = parentWidget()) {
        // Ancree en haut de la fenetre plutot qu'au centre : elle recouvre
        // ainsi les menus, pas le dessin qu'on est en train de regarder.
        const QPoint topLeft = anchor->mapToGlobal(QPoint((anchor->width() - width()) / 2,
                                                          anchor->height() / 8));
        move(topLeft);
    }
    show();
    m_search->setFocus();
}

void CommandPalette::refresh()
{
    const QString needle = m_search->text();

    QVector<QPair<int, int>> ranked; // score, rang
    for (int i = 0; i < m_entries.size(); ++i) {
        const int value = score(needle, m_entries.at(i));
        if (value >= 0)
            ranked.append({ value, i });
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const QPair<int, int> &a, const QPair<int, int> &b) {
                         return a.first < b.first;
                     });

    m_visible.clear();
    m_list->clear();
    for (const auto &entry : ranked) {
        const Entry &e = m_entries.at(entry.second);
        m_visible.append(entry.second);

        auto *item = new QListWidgetItem(e.title);
        item->setData(kGroupRole, e.group);
        item->setData(kShortcutRole, e.shortcut);
        item->setData(kDetailRole, e.detail);
        if (!e.detail.isEmpty())
            item->setToolTip(e.detail);
        // Une commande hors de portee reste visible mais grisee : la cacher
        // ferait croire qu'elle n'existe pas.
        if (!e.enabled)
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        m_list->addItem(item);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

QVector<CommandPalette::Entry> CommandPalette::visibleEntries() const
{
    QVector<Entry> out;
    out.reserve(m_visible.size());
    for (int index : m_visible)
        out.append(m_entries.at(index));
    return out;
}

void CommandPalette::runCurrent()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_visible.size())
        return;
    const Entry &entry = m_entries.at(m_visible.at(row));
    if (!entry.enabled || !entry.run)
        return;
    // La palette se ferme avant d'agir : la commande peut ouvrir sa propre
    // boite, et deux fenetres empilees desorientent.
    accept();
    entry.run();
}

} // namespace dsn
