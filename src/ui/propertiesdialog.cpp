#include "propertiesdialog.h"

#include "propertiespanel.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace dsn {

PropertiesDialog::PropertiesDialog(Document *document, const QSet<QString> &selection,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Propriétés"));
    // La hauteur est celle d'une fiche, pas d'un panneau plein ecran : ce qui
    // ne tient pas defile, comme dans l'ancien panneau.
    resize(420, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, Theme::space(2));
    m_panel = new PropertiesPanel(document, this);
    layout->addWidget(m_panel, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
    layout->setAlignment(buttons, Qt::AlignRight);

    setSelection(selection);
}

void PropertiesDialog::setSelection(const QSet<QString> &selection)
{
    m_panel->setSelection(selection);
}

} // namespace dsn
