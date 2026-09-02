#include "docktitle.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

namespace dsn {

DockTitle::DockTitle(const QString &title, const QString &hint, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(Theme::space(3), Theme::space(2), Theme::space(1),
                               Theme::space(2));
    layout->setSpacing(Theme::space(1));

    m_label = new QLabel(title.toUpper(), this);
    Theme::engrave(m_label);
    layout->addWidget(m_label, 1);

    m_close = new QToolButton(this);
    // Sans cette propriete, le padding general des QToolButton ecrase l'icone
    // a deux pixels de large dans un bouton de vingt : elle disparait sans
    // que rien ne le signale. Le meme piege a deja coute au ruban.
    m_close->setProperty("dockClose", true);
    m_close->setAutoRaise(true);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setToolTip(hint);
    m_close->setIconSize(QSize(14, 14));
    m_close->setFixedSize(20, 20);
    connect(m_close, &QToolButton::clicked, this, &DockTitle::closeRequested);
    layout->addWidget(m_close);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    applyTheme();
}

void DockTitle::applyTheme()
{
    m_label->setFont(Theme::engravedFont());
    m_close->setIcon(Icons::icon(Icons::Glyph::Collapse));
    update();
}

void DockTitle::paintEvent(QPaintEvent *)
{
    // Le fond du panneau et le filet du bas — exactement ce que la feuille de
    // style posait sur QDockWidget::title, qu'une barre de titre
    // personnalisee remplace et n'herite donc plus.
    QPainter painter(this);
    painter.fillRect(rect(), Theme::colors().surface);
    painter.fillRect(0, height() - 1, width(), 1, Theme::colors().border);
}

} // namespace dsn
