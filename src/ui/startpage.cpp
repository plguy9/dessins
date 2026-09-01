#include "startpage.h"

#include "theme.h"

#include <QAbstractItemDelegate>
#include <QCheckBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QVBoxLayout>

namespace dsn {

namespace {

// Les gestes qui changent vraiment la facon de travailler. La liste est
// courte volontairement : dix conseils ne s'apprennent pas, quatre si.
struct Tip {
    const char *keys;
    const char *what;
};

const Tip kTips[] = {
    { QT_TRANSLATE_NOOP("dsn::StartPage", "W puis un clic"),
      QT_TRANSLATE_NOOP("dsn::StartPage", "tracer un fil — tapez ensuite la cote (50, @10,5) "
                                          "au lieu de viser à la souris") },
    { QT_TRANSLATE_NOOP("dsn::StartPage", "survol d'un point"),
      QT_TRANSLATE_NOOP("dsn::StartPage", "reste un instant sur le milieu d'un fil : le point "
                                          "est retenu et son alignement se suit (F11)") },
    { QT_TRANSLATE_NOOP("dsn::StartPage", "double-clic sur un appareil"),
      QT_TRANSLATE_NOOP("dsn::StartPage", "repère, description, catalogue fabricant et "
                                          "rattachement, en un écran") },
    { QT_TRANSLATE_NOOP("dsn::StartPage", "Ctrl + Maj + P"),
      QT_TRANSLATE_NOOP("dsn::StartPage", "la palette de commandes : tout ce que le logiciel "
                                          "sait faire, cherché par son nom") },
};

// Le bandeau de marque. C'est le seul aplat colore du logiciel : partout
// ailleurs l'accent est tenu en reserve pour designer ce qui est actif. Ici
// il a le droit de respirer, parce que c'est le premier ecran et qu'il ne
// contient aucune commande a lire.
class BrandBand : public QWidget
{
public:
    explicit BrandBand(QWidget *parent) : QWidget(parent)
    {
        setFixedHeight(112);
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const ThemeColors &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Un fond qui descend du plan le plus profond vers le chrome : le
        // bandeau se fond dans la page au lieu d'y etre colle.
        QLinearGradient ground(0, 0, 0, height());
        ground.setColorAt(0.0, c.dark ? c.canvas : c.surface);
        ground.setColorAt(1.0, c.window);
        p.fillRect(rect(), ground);

        // La lueur de l'arc, derriere le sigle. Elle est large et tres
        // diluee : on la voit sans la regarder.
        QRadialGradient glow(QPointF(width() * 0.10, height() * 0.46), height() * 1.15);
        QColor tint = c.accent;
        tint.setAlpha(c.dark ? 54 : 26);
        glow.setColorAt(0.0, tint);
        tint.setAlpha(0);
        glow.setColorAt(1.0, tint);
        p.fillRect(rect(), glow);

        // Le filet de pied : la meme ligne que partout ailleurs.
        p.setPen(c.border);
        p.drawLine(0, height() - 1, width(), height() - 1);
    }
};

// Les projets recents. Le nom sur une ligne, le chemin dessous en retrait :
// deux dossiers peuvent porter le meme nom, et ouvrir le mauvais fait perdre
// plus de temps qu'il n'en gagne.
class RecentDelegate : public QAbstractItemDelegate
{
public:
    using QAbstractItemDelegate::QAbstractItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(220, 50);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const ThemeColors &c = Theme::colors();
        const bool enabled = index.flags().testFlag(Qt::ItemIsEnabled);
        const bool hover = option.state.testFlag(QStyle::State_MouseOver) && enabled;
        const bool selected = option.state.testFlag(QStyle::State_Selected) && enabled;

        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF row = QRectF(option.rect).adjusted(2, 1, -2, -1);
        if (hover || selected) {
            QColor fill = selected ? c.accent : c.text;
            fill.setAlpha(selected ? (c.dark ? 40 : 28) : (c.dark ? 16 : 14));
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(row, Theme::radius(), Theme::radius());
        }

        const QString name = index.data(Qt::DisplayRole).toString();
        const QString path = index.data(Qt::UserRole + 1).toString();

        QFont nameFont = Theme::uiFont(10, int(QFont::DemiBold));
        p->setFont(nameFont);
        p->setPen(!enabled ? c.textFaint : (selected ? c.accent : c.text));
        const QRectF top(row.left() + 12, row.top() + 6, row.width() - 24, 18);
        p->drawText(top, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(name, Qt::ElideMiddle, int(top.width())));

        QFont pathFont = Theme::monoFont(8.0);
        p->setFont(pathFont);
        p->setPen(c.textFaint);
        const QRectF bottom(row.left() + 12, row.top() + 24, row.width() - 24, 16);
        p->drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(path, Qt::ElideMiddle, int(bottom.width())));
    }
};

// Une action de depart : icone, titre, phrase. C'est un bouton, mais peint
// comme les lignes de la liste d'a cote — l'ecran d'accueil ne propose qu'une
// seule sorte de geste, cliquer sur une ligne. QPushButton ne sait pas
// afficher deux niveaux de texte : d'ou le trace a la main.
class ActionCard : public QPushButton
{
public:
    ActionCard(Icons::Glyph glyph, const QString &title, const QString &subtitle,
               QWidget *parent)
        : QPushButton(parent), m_glyph(glyph), m_title(title), m_subtitle(subtitle)
    {
        setFlat(true);
        setCursor(Qt::PointingHandCursor);
        // Hauteur fixe : sans cela une colonne un peu chargee ecraserait les
        // cartes avant de deborder, et la phrase de chaque action passerait
        // a la trappe.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setToolTip(subtitle);
        setAccessibleName(title);
        setText(title); // pour le clavier et les lecteurs d'ecran
    }

    // La hauteur passe par sizeHint et non par setMinimumHeight : la feuille
    // de style de l'application declare un « min-height » pour les boutons,
    // et Qt s'en sert pour reecrire la taille minimale du widget — un
    // setMinimumHeight pose ici serait efface sans bruit.
    QSize sizeHint() const override { return QSize(280, 54); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const ThemeColors &c = Theme::colors();
        const bool over = underMouse() || hasFocus();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(isDown() ? c.elevated : c.surface);
        p.setPen(QPen(over ? c.accent : c.border, 1.0));
        p.drawRoundedRect(card, Theme::radius(), Theme::radius());

        const QRectF icon(card.left() + 14, card.center().y() - 11, 22, 22);
        Icons::icon(m_glyph, c.accent).paint(&p, icon.toRect());

        const double textLeft = icon.right() + 14;
        QFont title = Theme::uiFont(10, int(QFont::DemiBold));
        p.setFont(title);
        p.setPen(c.text);
        p.drawText(QRectF(textLeft, card.top() + 8, card.width() - textLeft - 12, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, m_title);

        QFont sub = Theme::uiFont(9);
        p.setFont(sub);
        p.setPen(c.textMuted);
        const QRectF line(textLeft, card.top() + 27, card.width() - textLeft - 12, 18);
        p.drawText(line, Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(m_subtitle, Qt::ElideRight, int(line.width())));
    }

private:
    Icons::Glyph m_glyph;
    QString m_title;
    QString m_subtitle;
};

QLabel *sectionLabel(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text.toUpper(), parent);
    Theme::engrave(label);
    return label;
}

} // namespace

StartPage::StartPage(const QStringList &recentFiles, const QString &examplePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Arcus"));
    resize(900, 660);

    const ThemeColors &c = Theme::colors();

    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(0, 0, 0, 0);
    page->setSpacing(0);

    // ---- le bandeau de marque -------------------------------------------
    auto *band = new BrandBand(this);
    auto *header = new QHBoxLayout(band);
    header->setContentsMargins(Theme::space(8), 0, Theme::space(8), 0);
    header->setSpacing(Theme::space(5));

    auto *mark = new QLabel(band);
    mark->setPixmap(Icons::appIcon().pixmap(56, 56));
    mark->setFixedSize(56, 56);
    header->addWidget(mark, 0, Qt::AlignVCenter);

    auto *wordmark = new QLabel(band);
    // Le mot est espace comme sur un cartouche : c'est une marque gravee,
    // pas un titre de fenetre.
    wordmark->setText(QStringLiteral("<div style='font-size:30px; font-weight:600; "
                                     "letter-spacing:8px; color:%1'>ARCUS</div>"
                                     "<div style='margin-top:4px; letter-spacing:1px; "
                                     "color:%2'>%3</div>")
                              .arg(c.text.name(), c.textMuted.name(),
                                   tr("Schémas électriques — commande, puissance, unifilaires")));
    header->addWidget(wordmark, 1, Qt::AlignVCenter);
    page->addWidget(band);

    // ---- le corps --------------------------------------------------------
    auto *body = new QGridLayout;
    body->setContentsMargins(Theme::space(8), Theme::space(6), Theme::space(8), Theme::space(4));
    body->setHorizontalSpacing(Theme::space(9));
    body->setVerticalSpacing(Theme::space(3));
    page->addLayout(body, 1);

    // Colonne gauche : reprendre un projet.
    body->addWidget(sectionLabel(this, tr("Reprendre")), 0, 0);

    m_recent = new QListWidget(this);
    m_recent->setItemDelegate(new RecentDelegate(m_recent));
    m_recent->setFrameShape(QFrame::NoFrame);
    m_recent->setMouseTracking(true);
    // Fond transparent : une liste courte ne doit pas laisser une grande
    // boite vide sous elle. Ce sont des lignes posees sur la page, pas un
    // conteneur.
    m_recent->setStyleSheet(QStringLiteral("background: transparent;"));
    m_recent->setSelectionMode(QAbstractItemView::NoSelection);
    m_recent->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    for (const QString &path : recentFiles) {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(info.fileName());
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, info.absolutePath());
        if (!info.exists()) {
            // Un projet deplace ou efface reste liste, mais grise : le retirer
            // en silence laisserait croire qu'on ne l'a jamais ouvert.
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("Introuvable — le fichier a été déplacé ou supprimé."));
        }
        m_recent->addItem(item);
    }
    if (recentFiles.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("Aucun projet récent"));
        empty->setData(Qt::UserRole + 1, tr("les projets ouverts apparaîtront ici"));
        empty->setFlags(Qt::NoItemFlags);
        m_recent->addItem(empty);
    }
    body->addWidget(m_recent, 1, 0);

    // Colonne droite : commencer quelque chose, puis les quatre gestes.
    body->addWidget(sectionLabel(this, tr("Commencer")), 0, 1);

    auto *right = new QVBoxLayout;
    right->setSpacing(Theme::space(2));
    auto *newProject = new ActionCard(Icons::Glyph::New, tr("Nouveau projet"),
                                      tr("un folio A3, cadre et cartouche déjà posés"), this);
    auto *browse = new ActionCard(Icons::Glyph::Open, tr("Ouvrir un projet…"),
                                  tr("fichiers .arcus et .dsn"), this);
    right->addWidget(newProject);
    right->addWidget(browse);
    if (!examplePath.isEmpty() && QFileInfo::exists(examplePath)) {
        auto *example = new ActionCard(Icons::Glyph::Folios, tr("Projet d'exemple"),
                                       tr("démarrage direct d'un moteur, puissance et commande"),
                                       this);
        right->addWidget(example);
        connect(example, &QPushButton::clicked, this, [this, examplePath] {
            Q_EMIT openRequested(examplePath);
            accept();
        });
    }

    right->addStretch(1);
    body->addLayout(right, 1, 1);
    body->setColumnStretch(0, 4);
    body->setColumnStretch(1, 5);

    // ---- les quatre gestes, en bandeau -----------------------------------
    //
    // Sur toute la largeur plutot qu'en colonne : quatre conseils cote a cote
    // se parcourent d'un regard, empiles ils se lisent comme une notice.
    auto *tips = new QVBoxLayout;
    tips->setContentsMargins(Theme::space(8), Theme::space(4), Theme::space(8),
                             Theme::space(4));
    tips->setSpacing(Theme::space(2));
    tips->addWidget(sectionLabel(this, tr("Quatre gestes qui changent tout")));

    auto *row = new QHBoxLayout;
    row->setSpacing(Theme::space(6));
    for (const Tip &tip : kTips) {
        auto *line = new QLabel(
                QStringLiteral("<div style='color:%1; font-weight:600'>%2</div>"
                               "<div style='margin-top:2px; color:%3'>%4</div>")
                        .arg(c.text.name(), tr(tip.keys), c.textMuted.name(), tr(tip.what)),
                this);
        line->setWordWrap(true);
        line->setAlignment(Qt::AlignTop);
        row->addWidget(line, 1);
    }
    tips->addLayout(row);
    page->addLayout(tips);

    // ---- bas de page ------------------------------------------------------
    auto *rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setStyleSheet(QStringLiteral("color: %1;").arg(c.border.name()));
    rule->setFixedHeight(1);
    page->addWidget(rule);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(Theme::space(8), Theme::space(3), Theme::space(8),
                               Theme::space(4));
    auto *hide = new QCheckBox(tr("Ne plus afficher cet écran"), this);
    footer->addWidget(hide);
    footer->addStretch(1);
    auto *close = new QPushButton(tr("Fermer"), this);
    close->setDefault(true);
    close->setMinimumWidth(110);
    footer->addWidget(close);
    page->addLayout(footer);

    connect(hide, &QCheckBox::toggled, this, [this](bool on) { m_dismissed = on; });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(newProject, &QPushButton::clicked, this, [this] {
        Q_EMIT newProjectRequested();
        accept();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        Q_EMIT browseRequested();
        accept();
    });
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        Q_EMIT openRequested(path);
        accept();
    });
    // Un seul clic ouvre : c'est un ecran d'accueil, pas un gestionnaire de
    // fichiers — on n'y selectionne rien pour le plaisir de selectionner.
    connect(m_recent, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        Q_EMIT openRequested(path);
        accept();
    });
}

} // namespace dsn
