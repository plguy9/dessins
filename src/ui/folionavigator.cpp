#include "folionavigator.h"

#include "core/documentcommands.h"
#include "render/pdfexport.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace dsn {

namespace {
constexpr int kThumbWidth = 104;
}

FolioNavigator::FolioNavigator(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(kThumbWidth, kThumbWidth * 3 / 4));
    m_list->setSpacing(2);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    auto makeButton = [&](const QString &text, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        buttons->addWidget(button);
        return button;
    };
    QToolButton *add = makeButton(QStringLiteral("+"), tr("Ajouter un folio"));
    QToolButton *duplicate = makeButton(QStringLiteral("⧉"), tr("Dupliquer le folio"));
    m_remove = makeButton(QStringLiteral("−"), tr("Supprimer le folio"));
    buttons->addStretch(1);
    m_up = makeButton(QStringLiteral("↑"), tr("Monter"));
    m_down = makeButton(QStringLiteral("↓"), tr("Descendre"));
    layout->addLayout(buttons);

    connect(add, &QToolButton::clicked, this, &FolioNavigator::addFolio);
    connect(duplicate, &QToolButton::clicked, this, &FolioNavigator::duplicateFolio);
    connect(m_remove, &QToolButton::clicked, this, &FolioNavigator::removeFolio);
    connect(m_up, &QToolButton::clicked, this, [this] { moveFolio(-1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveFolio(1); });

    // Menu contextuel de la liste : les memes gestes que les boutons, la ou
    // se trouve deja le curseur quand on manipule le dossier.
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            &FolioNavigator::showListContextMenu);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0)
            m_document->setCurrentFolioIndex(row);
    });

    connect(m_document, &Document::folioListChanged, this, &FolioNavigator::refresh);
    connect(m_document, &Document::currentFolioChanged, this, [this](int index) {
        m_updating = true;
        m_list->setCurrentRow(index);
        m_updating = false;
    });
    // Le contenu change bien plus souvent que la liste : les vignettes se
    // rafraichissent en differe pour ne pas peindre un folio a chaque frappe.
    connect(m_document, &Document::changed, this, &FolioNavigator::scheduleThumbnails);

    refresh();
}

void FolioNavigator::refresh()
{
    m_updating = true;
    m_list->clear();
    for (int i = 0; i < m_document->folioCount(); ++i) {
        const Folio *folio = m_document->project().folioAt(i);
        const QString caption = folio->title.isEmpty()
                ? tr("Folio %1").arg(folio->number)
                : QStringLiteral("%1 — %2").arg(folio->number, folio->title);
        m_list->addItem(caption);
    }
    m_list->setCurrentRow(m_document->currentFolioIndex());
    m_updating = false;

    const bool several = m_document->folioCount() > 1;
    m_remove->setEnabled(several);
    m_up->setEnabled(several);
    m_down->setEnabled(several);

    scheduleThumbnails();
}

void FolioNavigator::scheduleThumbnails()
{
    QTimer::singleShot(150, this, [this] {
        for (int i = 0; i < m_list->count() && i < m_document->folioCount(); ++i) {
            const Folio *folio = m_document->project().folioAt(i);
            RenderStyle style = RenderStyle::print();
            style.showZoneLabels = false;
            style.showWireNumbers = false;
            style.showValues = false;
            const double ppm = kThumbWidth / std::max(1.0, folio->sheet.width);
            const QImage image = PdfExport::renderFolio(m_document->project(), *folio, style, ppm);
            m_list->item(i)->setIcon(QIcon(QPixmap::fromImage(image)));
        }
    });
}

void FolioNavigator::addFolio()
{
    auto folio = std::make_unique<Folio>();
    folio->number = QString::number(m_document->folioCount() + 1);
    folio->title = tr("Nouveau folio");
    if (const Folio *current = m_document->currentFolio()) {
        // Le nouveau folio herite du format et du cadre du folio courant :
        // un dossier melange rarement les formats sans raison.
        folio->sheet = current->sheet;
        folio->frame = current->frame;
    }
    const QString id = folio->id();
    m_document->push(std::make_unique<AddFolioCommand>(m_document->project(), std::move(folio)));
    refresh();
    m_document->setCurrentFolioIndex(m_document->project().indexOf(id));
    Q_EMIT statusMessage(tr("Folio ajouté"));
}

void FolioNavigator::duplicateFolio()
{
    const Folio *current = m_document->currentFolio();
    if (!current)
        return;

    auto copy = std::make_unique<Folio>(*current);
    copy->setId(newId());
    copy->number = QString::number(m_document->folioCount() + 1);
    copy->title = tr("%1 (copie)").arg(current->title);
    // Les entites dupliquees doivent recevoir de nouveaux identifiants, sans
    // quoi la connectivite melangerait les deux folios.
    for (const EntityPtr &entity : copy->entities())
        entity->setId(newId());

    const QString id = copy->id();
    m_document->push(std::make_unique<AddFolioCommand>(m_document->project(), std::move(copy),
                                                       m_document->currentFolioIndex() + 1));
    refresh();
    m_document->setCurrentFolioIndex(m_document->project().indexOf(id));
    Q_EMIT statusMessage(tr("Folio dupliqué"));
}

void FolioNavigator::renameFolio()
{
    const Folio *current = m_document->currentFolio();
    if (!current)
        return;

    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("Renommer le folio"),
                                                tr("Titre du folio :"), QLineEdit::Normal,
                                                current->title, &ok);
    if (!ok)
        return;
    const QString number = QInputDialog::getText(this, tr("Renommer le folio"),
                                                 tr("Numéro affiché :"), QLineEdit::Normal,
                                                 current->number, &ok);
    if (!ok)
        return;

    m_document->push(std::make_unique<RenameFolioCommand>(m_document->project(), current->id(),
                                                          number, title));
    refresh();
    Q_EMIT statusMessage(tr("Folio renommé"));
}

void FolioNavigator::showListContextMenu(const QPoint &pos)
{
    // Le clic droit designe d'abord le folio vise : sans cela le menu
    // agirait sur celui qui etait courant, pas sur celui qu'on montre.
    if (QListWidgetItem *item = m_list->itemAt(pos))
        m_list->setCurrentItem(item);

    QMenu menu(this);
    menu.addAction(tr("Ajouter un folio"), this, &FolioNavigator::addFolio);
    menu.addAction(tr("Dupliquer"), this, &FolioNavigator::duplicateFolio);
    menu.addAction(tr("Renommer…"), this, &FolioNavigator::renameFolio);
    menu.addSeparator();
    QAction *up = menu.addAction(tr("Monter"), this, [this] { moveFolio(-1); });
    QAction *down = menu.addAction(tr("Descendre"), this, [this] { moveFolio(1); });
    up->setEnabled(m_document->currentFolioIndex() > 0);
    down->setEnabled(m_document->currentFolioIndex() < m_document->folioCount() - 1);
    menu.addSeparator();
    menu.addAction(tr("Mise en page…"), this, [this] { Q_EMIT pageSetupRequested(); });
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Supprimer"), this, &FolioNavigator::removeFolio);
    // Un dossier sans folio n'existe pas : le dernier ne se supprime jamais.
    remove->setEnabled(m_document->folioCount() > 1);

    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void FolioNavigator::removeFolio()
{
    const Folio *current = m_document->currentFolio();
    if (!current || m_document->folioCount() <= 1)
        return;
    const int index = m_document->currentFolioIndex();
    m_document->push(std::make_unique<RemoveFolioCommand>(m_document->project(), current->id()));
    refresh();
    m_document->setCurrentFolioIndex(std::min(index, m_document->folioCount() - 1));
    Q_EMIT statusMessage(tr("Folio supprimé"));
}

void FolioNavigator::moveFolio(int delta)
{
    const int from = m_document->currentFolioIndex();
    const int to = from + delta;
    if (to < 0 || to >= m_document->folioCount())
        return;
    m_document->push(std::make_unique<MoveFolioCommand>(m_document->project(), from, to));
    refresh();
    m_document->setCurrentFolioIndex(to);
}

} // namespace dsn
