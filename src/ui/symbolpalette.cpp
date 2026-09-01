#include "symbolpalette.h"

#include "render/foliopainter.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

namespace dsn {

namespace {
constexpr int kIconPixels = 44;
constexpr int kIdRole = Qt::UserRole + 1;
} // namespace

SymbolPalette::SymbolPalette(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Rechercher un symbole…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_category = new QComboBox(this);
    layout->addWidget(m_category);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(kIconPixels, kIconPixels));
    m_list->setUniformItemSizes(false);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setWordWrap(false);
    layout->addWidget(m_list, 1);

    connect(m_search, &QLineEdit::textChanged, this, &SymbolPalette::rebuildList);
    connect(m_category, &QComboBox::currentIndexChanged, this, &SymbolPalette::rebuildList);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            Q_EMIT symbolChosen(item->data(kIdRole).toString());
    });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item)
            Q_EMIT symbolActivated(item->data(kIdRole).toString());
    });
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
    const QString previous = m_category->currentText();
    m_category->blockSignals(true);
    m_category->clear();
    m_category->addItem(tr("Toutes les catégories"), QString());
    if (m_library) {
        const QStringList categories = m_library->categories(m_norm);
        for (const QString &category : categories)
            m_category->addItem(category, category);
    }
    const int index = m_category->findText(previous);
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

    QList<const SymbolDefinition *> definitions =
            needle.isEmpty() ? m_library->search(QString(), m_norm)
                             : m_library->search(needle, m_norm);

    const RenderStyle style = palette().color(QPalette::Window).lightness() < 128
            ? RenderStyle::screenDark()
            : RenderStyle::screen();

    for (const SymbolDefinition *definition : std::as_const(definitions)) {
        if (!category.isEmpty() && definition->category != category)
            continue;
        auto *item = new QListWidgetItem(renderIcon(*definition, kIconPixels, style),
                                         definition->name, m_list);
        item->setData(kIdRole, definition->id);
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
}

} // namespace dsn
