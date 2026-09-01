#include "startpage.h"

#include "theme.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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

} // namespace

StartPage::StartPage(const QStringList &recentFiles, const QString &examplePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Dessins"));
    resize(760, 520);

    const ThemeColors &c = Theme::colors();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(18);

    auto *title = new QLabel(tr("<h2 style='margin:0'>Dessins</h2>"
                                "<p style='margin:2px 0 0 0; color:%1'>"
                                "Schémas électriques — commande, puissance, unifilaires</p>")
                                     .arg(c.textMuted.name()),
                             this);
    layout->addWidget(title);

    auto *columns = new QGridLayout;
    columns->setHorizontalSpacing(24);
    layout->addLayout(columns, 1);

    // ---- colonne gauche : ouvrir quelque chose ------------------------
    auto *openLabel = new QLabel(tr("<b>Ouvrir</b>"), this);
    columns->addWidget(openLabel, 0, 0);

    m_recent = new QListWidget(this);
    m_recent->setAlternatingRowColors(true);
    for (const QString &path : recentFiles) {
        const QFileInfo info(path);
        // Le chemin sous le nom : deux dossiers peuvent porter le meme nom, et
        // ouvrir le mauvais fait perdre plus de temps qu'il n'en gagne.
        auto *item = new QListWidgetItem(info.fileName() + QStringLiteral("\n")
                                         + info.absolutePath());
        item->setData(Qt::UserRole, path);
        if (!info.exists()) {
            // Un projet deplace ou efface reste liste, mais grise : le retirer
            // en silence laisserait croire qu'on ne l'a jamais ouvert.
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("Introuvable — le fichier a été déplacé ou supprimé."));
        }
        m_recent->addItem(item);
    }
    if (recentFiles.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("Aucun projet récent."));
        empty->setFlags(Qt::NoItemFlags);
        m_recent->addItem(empty);
    }
    columns->addWidget(m_recent, 1, 0);

    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(8);
    auto *newProject = new QPushButton(tr("Nouveau projet"), this);
    auto *browse = new QPushButton(tr("Parcourir…"), this);
    buttons->addWidget(newProject);
    buttons->addWidget(browse);
    if (!examplePath.isEmpty() && QFileInfo::exists(examplePath)) {
        auto *example = new QPushButton(tr("Ouvrir le projet d'exemple"), this);
        example->setToolTip(tr("Démarrage direct d'un moteur, deux folios — de quoi voir "
                               "le logiciel à l'œuvre tout de suite."));
        buttons->addWidget(example);
        connect(example, &QPushButton::clicked, this, [this, examplePath] {
            Q_EMIT openRequested(examplePath);
            accept();
        });
    }
    buttons->addStretch(1);
    columns->addLayout(buttons, 1, 1);

    // ---- colonne droite : quatre gestes -------------------------------
    columns->addWidget(new QLabel(tr("<b>Quatre gestes qui changent tout</b>"), this), 0, 2);
    auto *tips = new QVBoxLayout;
    tips->setSpacing(10);
    for (const Tip &tip : kTips) {
        auto *line = new QLabel(
                QStringLiteral("<div style='margin-bottom:2px'><b>%1</b></div>"
                               "<div style='color:%2'>%3</div>")
                        .arg(tr(tip.keys), c.textMuted.name(), tr(tip.what)),
                this);
        line->setWordWrap(true);
        tips->addWidget(line);
    }
    tips->addStretch(1);
    columns->addLayout(tips, 1, 2);
    columns->setColumnStretch(0, 3);
    columns->setColumnStretch(2, 4);

    // ---- bas de page ---------------------------------------------------
    auto *footer = new QHBoxLayout;
    auto *hide = new QCheckBox(tr("Ne plus afficher cet écran"), this);
    footer->addWidget(hide);
    footer->addStretch(1);
    auto *close = new QPushButton(tr("Fermer"), this);
    close->setDefault(true);
    footer->addWidget(close);
    layout->addLayout(footer);

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
}

} // namespace dsn
