#include "commandpalette.h"

#include "theme.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
                          "#paletteList::item { padding: 7px 10px; border-radius: 6px; "
                          "  color: %3; }"
                          "#paletteList::item:selected { background: %4; color: %5; }"
                          "#paletteHint { color: %6; padding: 8px; border-top: 1px solid %2; "
                          "  font-size: 11px; }")
                          .arg(c.elevated.name(), c.border.name(), c.text.name(),
                               c.accent.name(), c.accentText.name(), c.textMuted.name()));

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

        QString label = e.title;
        if (!e.group.isEmpty())
            label = e.group + QStringLiteral("  ›  ") + e.title;
        if (!e.shortcut.isEmpty())
            label += QStringLiteral("        ") + e.shortcut;
        auto *item = new QListWidgetItem(label);
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
