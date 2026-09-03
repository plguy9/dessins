#include "symbolswapdialog.h"

#include "symbolpalette.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace dsn {

SymbolSwapDialog::SymbolSwapDialog(const SymbolLibrary *library, const QString &norm,
                                   const QString &currentDefinitionId,
                                   const QString &designation, QWidget *parent)
    : QDialog(parent), m_current(currentDefinitionId)
{
    setWindowTitle(tr("Remplacer le symbole"));
    setModal(true);
    resize(420, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(Theme::space(4), Theme::space(4), Theme::space(4),
                               Theme::space(4));
    layout->setSpacing(Theme::space(3));

    // Dire ce qui est garde, parce que c'est exactement ce dont on doute avant
    // de cliquer : est-ce que je vais perdre mon repere et mes fils ?
    auto *conseil = new QLabel(
            designation.isEmpty()
                    ? tr("Le repère, la position et les raccordements sont conservés.")
                    : tr("%1 garde son repère, sa position et ses raccordements.")
                              .arg(designation),
            this);
    conseil->setWordWrap(true);
    layout->addWidget(conseil);

    m_palette = new SymbolPalette(this);
    m_palette->setLibrary(library);
    m_palette->setNorm(norm);
    layout->addWidget(m_palette, 1);

    auto *boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    boutons->button(QDialogButtonBox::Ok)->setText(tr("Remplacer"));
    connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(boutons);

    // Le double-clic vaut validation : c'est le geste de la palette, et le
    // reapprendre ici serait une friction pour rien.
    connect(m_palette, &SymbolPalette::symbolActivated, this, &QDialog::accept);
}

QString SymbolSwapDialog::chosenDefinitionId() const
{
    const QString choisi = m_palette->currentDefinitionId();
    return choisi == m_current ? QString() : choisi;
}

} // namespace dsn
