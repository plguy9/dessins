// Les onglets de folio : les pages du dossier, en bas du canevas.
//
// Un folio est une PAGE, pas un outil. Sa metaphore juste est l'onglet de
// classeur, pas la colonne laterale : le navigateur prenait 193 px de large
// pour montrer, sur le projet d'exemple, un folio et beaucoup de vide — et il
// partageait cette colonne avec la palette de symboles, qui est le panneau le
// plus sollicite de la journee. Vingt-huit pixels de haut suffisent, et sur un
// dossier de trente pages une bande horizontale se parcourt a l'oeil quand une
// colonne se parcourt a l'ascenseur.
//
// Les vignettes ne disparaissent pas : elles se deplient a la demande sous les
// onglets, et c'est le meme FolioNavigator qu'avant. La vignette reste le vrai
// rendu du folio — c'est ce qui permet de retrouver la bonne page d'un coup
// d'oeil plutot qu'en lisant trente titres — et elle vaut d'autant plus depuis
// qu'elle s'accorde avec le canevas (refonte 01).
//
// Comme le ruban, cette barre NE DETIENT RIEN : chaque onglet represente un
// folio du Document et DEMANDE le changement, il ne le fait pas.
#pragma once

#include <QWidget>

class QToolButton;

namespace dsn {

class Document;
class FolioNavigator;
class FolioTabBar;

class FolioTabs : public QWidget
{
    Q_OBJECT

public:
    // La hauteur de la bande d'onglets, et celle de la bande de vignettes
    // quand elle est depliee. Elles vivent ici et nulle part ailleurs.
    static constexpr int kTabHeight = 28;
    static constexpr int kStripHeight = 126;

    explicit FolioTabs(Document *document, QWidget *parent = nullptr);

    // Le navigateur vit dans la bande depliee : la fenetre principale garde
    // le meme objet qu'avant, avec ses signaux et son menu contextuel.
    FolioNavigator *navigator() const { return m_strip; }

    // Relit le document. Appelee quand un folio est ajoute, renomme, deplace
    // ou supprime — la barre ne detient rien, elle relit.
    void refresh();
    // Le nombre d'onglets REELLEMENT poses. Un test lit ce compte plutot que
    // de compter des pixels — et c'est lui qui a attrape le defaut : la barre
    // ne suivait pas un folio ajoute par commande.
    int tabCount() const;
    // Le titre affiche par un onglet. Un test le lit : `tabCount` ne voit pas
    // un folio RENOMME, et c'est le renommage qui prouve la relecture — il
    // passe par une commande sans changer ni la liste ni la page courante.
    QString tabTitle(int index) const;

    bool stripVisible() const;
    void setStripVisible(bool visible);

    void applyTheme();

Q_SIGNALS:
    void folioChosen(const QString &folioId);
    void stripVisibleChanged(bool visible);

private:
    Document *m_document = nullptr;
    FolioTabBar *m_tabs = nullptr;
    QToolButton *m_expand = nullptr;
    QToolButton *m_add = nullptr;
    FolioNavigator *m_strip = nullptr;
    QWidget *m_stripHost = nullptr;
    bool m_updating = false;
};

} // namespace dsn
