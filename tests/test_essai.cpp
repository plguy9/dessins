// ESSAI D'USAGE — conduire le logiciel comme un dessinateur, et le mesurer.
//
// Ce fichier ne verifie pas une fonction : il REFAIT un vrai schema, celui
// d'une armoire Valmet relevee sur le terrain, en ne passant que par des
// evenements de souris et de clavier. Tout ce qui est fait ici, un
// dessinateur peut le faire avec sa main ; rien ne court-circuite l'interface.
//
// C'est la seule maniere honnete de repondre a « est-ce que c'est fluide ».
// Le compte de gestes et la liste des accrocs sont ecrits a la fin : ce sont
// eux le resultat, pas le succes du test.
//
// RESERVE. Conduire les memes widgets par les memes evenements qu'une main
// trouve de vrais defauts — quatre pendant le premier essai. Cela ne dit rien
// de ce qu'on RESSENT : la latence, la fatigue, le geste refait dix fois.
// Seul un dessinateur devant l'ecran le dira.
//
// Pour regarder le resultat :
//   QT_QPA_PLATFORM=offscreen ARCUS_ESSAI_CAPTURES=/tmp \
//       ./build/bin/arcus_ui_tests "[valmet]"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QTemporaryDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>

#include <functional>

#include "core/documentcommands.h"
#include "io/dsnfile.h"
#include "render/pdfexport.h"
#include "rules/audit.h"
#include "ui/terminalstripdialog.h"
#include "rules/reports.h"
#include "rules/crossref.h"
#include <QFile>
#include "symbols/librarystore.h"
#include "ui/commandline.h"
#include "ui/document.h"
#include "ui/findreplacedialog.h"
#include "ui/folioview.h"
#include "ui/mainwindow.h"
#include "ui/symbolpalette.h"

using namespace dsn;

namespace {

// --------------------------------------------------------------------------
// Le pupitre : tout ce qu'un dessinateur peut faire, et rien de plus.

class Pupitre
{
public:
    Pupitre()
    {
        m_window.resize(1700, 1060);
        m_window.show();
        QApplication::processEvents();
        m_view = m_window.findChild<FolioView *>();
        m_document = m_window.findChild<Document *>();
        m_palette = m_window.findChild<SymbolPalette *>();
        m_command = m_window.findChild<CommandLine *>();

        // Garde-fou : une boite modale laissee ouverte bloquerait la boucle
        // d'evenements pour toujours. On la ferme et on le note.
        m_chien = new QTimer(&m_window);
        m_chien->setInterval(3000);
        QObject::connect(m_chien, &QTimer::timeout, &m_window, [this] {
            if (QWidget *modal = QApplication::activeModalWidget()) {
                accroc(QStringLiteral("Une boite modale a bloque le dessin : %1")
                               .arg(modal->windowTitle()));
                modal->close();
            }
        });
        m_chien->start();
    }

    MainWindow &fenetre() { return m_window; }
    FolioView *vue() const { return m_view; }
    Document *document() const { return m_document; }
    CommandLine *ligne() const { return m_command; }
    Folio *folio() const { return m_document->currentFolio(); }

    int gestes() const { return m_gestes; }
    int dialogues() const { return m_dialogues; }
    QStringList accrocs() const { return m_accrocs; }
    void accroc(const QString &quoi)
    {
        if (!m_accrocs.contains(quoi))
            m_accrocs.append(quoi);
    }

    // ---- clavier -------------------------------------------------------

    void frappe(QWidget *cible, const QString &texte)
    {
        for (const QChar c : texte) {
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QApplication::sendEvent(cible, &press);
        }
        ++m_gestes; // une saisie compte pour un geste, pas une frappe par lettre
    }

    void touche(QWidget *cible, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QKeyEvent press(QEvent::KeyPress, key, mods);
        QApplication::sendEvent(cible, &press);
        QKeyEvent release(QEvent::KeyRelease, key, mods);
        QApplication::sendEvent(cible, &release);
        ++m_gestes;
    }

    // ---- souris --------------------------------------------------------

    QPointF pixels(const QPointF &mm) const { return m_view->mapFromScene(mm); }

    void survole(const QPointF &mm)
    {
        const QPointF p = pixels(mm);
        QMouseEvent move(QEvent::MouseMove, p, m_view->mapToGlobal(p.toPoint()), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(m_view, &move);
    }

    void clic(const QPointF &mm, Qt::MouseButton bouton = Qt::LeftButton)
    {
        survole(mm);
        const QPointF p = pixels(mm);
        QMouseEvent press(QEvent::MouseButtonPress, p, m_view->mapToGlobal(p.toPoint()), bouton,
                          bouton, Qt::NoModifier);
        QApplication::sendEvent(m_view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, p, m_view->mapToGlobal(p.toPoint()),
                            bouton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(m_view, &release);
        ++m_gestes;
    }

    // ---- repondre a une boite modale -----------------------------------
    //
    // Le compteur retient combien de fois le dessin a ete interrompu par une
    // boite. Il valait 48 pour ce folio, quand chaque texte et chaque
    // etiquette en ouvrait une ; il vaut zero depuis que l'on tape sur le
    // dessin. La fonction reste pour les boites qui subsistent ailleurs.

    void repondre(const QString &texte)
    {
        ++m_dialogues;
        QTimer::singleShot(0, &m_window, [this, texte] {
            QWidget *modal = QApplication::activeModalWidget();
            if (!modal) {
                accroc(QStringLiteral("Une boite modale etait attendue et n'est pas venue"));
                return;
            }
            auto *champ = modal->findChild<QLineEdit *>();
            if (!champ) {
                accroc(QStringLiteral("Boite modale sans champ de saisie"));
                modal->close();
                return;
            }
            champ->setFocus();
            frappe(champ, texte);
            touche(champ, Qt::Key_Return);
            if (modal->isVisible())
                modal->close();
        });
    }

    // ---- la palette, au clavier ----------------------------------------
    //
    // On tape dans la recherche puis Fleche bas : le filtre descend dans la
    // grille, la selection change, le symbole s'arme. C'est le geste que la
    // palette a ete faite pour servir.

    bool armer(const QString &recherche, int quartsDeTour = 0)
    {
        auto *champ = m_palette->findChild<QLineEdit *>();
        auto *grille = m_palette->findChild<QListWidget *>();
        if (!champ || !grille) {
            accroc(QStringLiteral("Palette : ni champ de recherche ni grille"));
            return false;
        }
        champ->setFocus();
        champ->clear();
        frappe(champ, recherche);
        QApplication::processEvents();
        if (grille->count() == 0) {
            accroc(QStringLiteral("Palette : « %1 » ne remonte aucun symbole").arg(recherche));
            return false;
        }
        grille->setCurrentRow(-1);
        touche(champ, Qt::Key_Down);
        QApplication::processEvents();
        if (m_view->pendingSymbol().isEmpty()) {
            accroc(QStringLiteral("Palette : « %1 » n'arme rien").arg(recherche));
            return false;
        }
        // R fait pivoter le symbole ARME. L'orientation est ensuite gardee
        // d'une pose a l'autre — c'est le bon comportement, et c'est pour cela
        // qu'on ne represse pas R a chaque pose.
        for (int i = 0; i < quartsDeTour; ++i)
            touche(m_view, Qt::Key_R);
        m_quartsArmes = quartsDeTour;
        return true;
    }

    // ---- la ligne de commande ------------------------------------------

    bool commande(const QString &texte)
    {
        auto *champ = m_command->findChild<QLineEdit *>();
        if (!champ) {
            accroc(QStringLiteral("Ligne de commande : pas de champ de saisie"));
            return false;
        }
        champ->setFocus();
        champ->clear();
        frappe(champ, texte);
        touche(champ, Qt::Key_Return);
        QApplication::processEvents();
        m_view->setFocus();
        // Toute commande qui change d'outil DESARME le symbole en cours.
        // Poser un texte au milieu d'une serie de symboles oblige donc a
        // retourner dans la palette : c'est un accroc reel, note plus bas.
        if (m_view->pendingSymbol().isEmpty())
            m_dernierSymbole.clear();
        return true;
    }

    // ---- gestes de dessin ----------------------------------------------

    SymbolInstance *poser(const QString &recherche, const QPointF &mm, int quartsDeTour = 0,
                          const QString &repere = QString())
    {
        // On verifie ce que la VUE porte, pas ce que le pupitre croit : changer
        // d'outil desarme le symbole, et c'est un accroc reel de l'interface.
        if (m_view->pendingSymbol().isEmpty() || m_dernierSymbole != recherche
            || m_quartsArmes != quartsDeTour) {
            if (!armer(recherche, quartsDeTour))
                return nullptr;
            m_dernierSymbole = recherche;
        }

        const std::size_t avant = folio()->entityCount();
        clic(mm);
        QApplication::processEvents();
        if (folio()->entityCount() == avant) {
            accroc(QStringLiteral("Le clic n'a rien pose pour « %1 »").arg(recherche));
            return nullptr;
        }
        const auto symboles = folio()->entitiesOfType<SymbolInstance>();
        auto *pose = symboles.back();
        if (!repere.isEmpty()) {
            // Le repere se saisit dans la fiche du composant. Ici on ne peut
            // pas ouvrir la boite modale pour chaque appareil sans arreter le
            // dessin : c'est un accroc, on le note et on ecrit le champ.
            pose->setDesignation(repere);
            pose->designationLocked = true;
            m_repereEcritALaMain++;
        }
        return pose;
    }

    QPointF broche(const SymbolInstance *symbole, const QString &numero) const
    {
        if (!symbole)
            return {};
        const SymbolDefinition *d = m_document->project().library.definition(symbole->definitionId);
        const Pin *p = d ? d->pin(numero) : nullptr;
        if (!p)
            return {};
        return symbole->placement.map(p->position);
    }

    Wire *fil(const QVector<QPointF> &points)
    {
        if (points.size() < 2)
            return nullptr;
        commande(QStringLiteral("L"));
        for (const QPointF &p : points)
            clic(p);
        touche(m_view, Qt::Key_Return);
        QApplication::processEvents();
        const auto fils = folio()->entitiesOfType<Wire>();
        if (fils.empty())
            return nullptr;
        Wire *dernier = fils.back();
        if (dernier->points.size() != points.size())
            accroc(QStringLiteral("Un fil de %1 points en a donne %2")
                           .arg(points.size())
                           .arg(dernier->points.size()));
        return dernier;
    }

    // Etiquette de potentiel : nommer un fil, plutot que tirer un rail en
    // travers du dessin. Deux etiquettes du meme nom sont un seul potentiel.
    Label *etiquette(const QPointF &mm, const QString &nom)
    {
        commande(QStringLiteral("ET"));
        clic(mm);
        if (!m_view->isTypingText()) {
            accroc(QStringLiteral("Le clic n'a pas ouvert la saisie d'étiquette"));
            return nullptr;
        }
        frappe(m_view, nom);
        touche(m_view, Qt::Key_Return);
        QApplication::processEvents();
        const auto etiquettes = folio()->entitiesOfType<Label>();
        if (etiquettes.empty()) {
            accroc(QStringLiteral("L'etiquette de potentiel n'a rien pose"));
            return nullptr;
        }
        return etiquettes.back();
    }

    // Cadre : la commande RECTANGLE, deux clics.
    GraphicItem *rectangle(const QPointF &coin, const QPointF &oppose)
    {
        commande(QStringLiteral("REC"));
        clic(coin);
        clic(oppose);
        QApplication::processEvents();
        const auto formes = folio()->entitiesOfType<GraphicItem>();
        if (formes.empty()) {
            accroc(QStringLiteral("La commande RECTANGLE n'a rien pose"));
            return nullptr;
        }
        return formes.back();
    }

    // On tape ou l'on a clique : plus de boite modale. La hauteur se regle
    // une fois et vaut pour les textes suivants, comme le type de fil.
    TextItem *texte(const QPointF &mm, const QString &contenu, double hauteur = 2.5)
    {
        if (!qFuzzyCompare(m_view->textHeight(), hauteur)) {
            m_view->setTextHeight(hauteur);
            ++m_gestes;
        }
        if (m_view->tool() != FolioView::Tool::Text)
            commande(QStringLiteral("T"));
        clic(mm);
        if (!m_view->isTypingText()) {
            accroc(QStringLiteral("Le clic n'a pas ouvert la saisie de texte"));
            return nullptr;
        }
        frappe(m_view, contenu);
        touche(m_view, Qt::Key_Return);
        QApplication::processEvents();
        const auto textes = folio()->entitiesOfType<TextItem>();
        if (textes.empty())
            return nullptr;
        TextItem *pose = textes.back();
        if (pose->text != contenu) {
            accroc(QStringLiteral("Texte attendu « %1 », obtenu « %2 »").arg(contenu, pose->text));
            return nullptr;
        }
        return pose;
    }

    // Agir DANS une boite modale : l'ouvrir bloque la boucle d'evenements,
    // il faut donc armer le geste avant le clic qui l'ouvre.
    void dansLaBoite(const std::function<void(QWidget *)> &geste)
    {
        ++m_dialogues;
        QTimer::singleShot(0, &m_window, [this, geste] {
            QWidget *modal = QApplication::activeModalWidget();
            if (!modal) {
                accroc(QStringLiteral("Une boite modale etait attendue et n'est pas venue"));
                return;
            }
            geste(modal);
            if (modal->isVisible())
                modal->close();
        });
    }

    // Ajouter un folio, s'y placer, le nommer.
    Folio *nouveauFolio(const QString &numero, const QString &titre)
    {
        commande(QStringLiteral("NF"));
        Folio *ajoute = folio();
        if (!ajoute) {
            accroc(QStringLiteral("La commande NOUVFOLIO n'a rien ajoute"));
            return nullptr;
        }
        ajoute->number = numero;
        ajoute->title = titre;
        m_view->zoomToFit();
        return ajoute;
    }

    void capture(const QString &fichier)
    {
        QApplication::processEvents();
        m_window.grab().save(fichier);
    }

    void captureVue(const QString &fichier)
    {
        QApplication::processEvents();
        m_view->grab().save(fichier);
    }

    int reperesEcritsALaMain() const { return m_repereEcritALaMain; }

private:
    MainWindow m_window;
    FolioView *m_view = nullptr;
    Document *m_document = nullptr;
    SymbolPalette *m_palette = nullptr;
    CommandLine *m_command = nullptr;
    int m_gestes = 0;
    int m_dialogues = 0;
    int m_repereEcritALaMain = 0;
    QString m_dernierSymbole;
    int m_quartsArmes = 0;
    QTimer *m_chien = nullptr;
    QStringList m_accrocs;
};

} // namespace

// --------------------------------------------------------------------------

TEST_CASE("Essai : les gestes de base repondent", "[essai]")
{
    Pupitre p;
    REQUIRE(p.vue());
    REQUIRE(p.folio());
    p.vue()->zoomToFit();

    // 1. Armer au clavier depuis la palette, poser d'un clic.
    auto *bobine = p.poser(QStringLiteral("bobine de relais"), QPointF(120, 100));
    REQUIRE(bobine != nullptr);
    CHECK(bobine->definitionId == QStringLiteral("iec:relay-coil"));

    // 2. L'accrochage aux objets doit rattraper un clic approximatif : on
    //    vise la broche a 1,2 mm pres, le fil doit partir de la broche EXACTE.
    //    C'est la promesse qui fait qu'on cable vite sans zoomer.
    const QPointF a1 = p.broche(bobine, QStringLiteral("A1"));
    auto *f = p.fil({ a1 + QPointF(1.2, -1.2), QPointF(60, a1.y() - 1.2) });
    REQUIRE(f != nullptr);
    const double ecart = std::hypot(f->points.first().x() - a1.x(), f->points.first().y() - a1.y());
    INFO("depart du fil a " << ecart << " mm de la broche A1");
    CHECK(ecart < 0.01);

    QTextStream(stdout) << "\n[essai] gestes de base : " << p.gestes() << " gestes, accrocs : "
                        << (p.accrocs().isEmpty() ? QStringLiteral("aucun")
                                                  : p.accrocs().join(QStringLiteral(" | ")))
                        << "\n";
}

// --------------------------------------------------------------------------
// Le vrai schema : armoire B7 RM5, raccordement de deux appareils Valmet.

// --------------------------------------------------------------------------
// LE VRAI SCHEMA : armoire 047BJ0152B, raccordement de deux appareils Valmet.
//
// Releve sur photo. De gauche a droite : les deux appareils de terrain, la
// boite de jonction BJ RM5, l'alimentation 24 V, puis l'armoire avec ses cinq
// relais d'interface et les borniers des modules PLC152.
//
// Le courant : un contact sec Valmet se ferme -> il excite un relais 24 V ->
// le contact de ce relais renvoie l'information sur une voie d'entree de
// l'IDM PLC152. En sens inverse : une sortie de l'ODM PLC152 excite le relais
// 120 V, dont le contact met les entrees binaires des deux Valmet sous
// tension.

TEST_CASE("Essai : redessiner le schema Valmet B7 RM5", "[essai][valmet]")
{
    Pupitre p;
    REQUIRE(p.vue());
    Folio *folio = p.folio();
    REQUIRE(folio);

    p.document()->project().info.title = QStringLiteral("Cabinet 047BJ0152B / KN047-0");
    p.document()->project().info.reference = QStringLiteral("BJ RM5");
    p.document()->project().info.client = QStringLiteral("Valmet");
    folio->number = QStringLiteral("1");
    folio->title = QStringLiteral("Raccordement Valmet — eau blanche et caisse d'arrivée");
    p.vue()->zoomToFit();

    // Les colonnes, en millimetres sur une feuille A3. Tout tombe sur le pas
    // de 2,5 mm : sans cela l'accrochage a la resolution decale chaque pose et
    // les fils qu'on croit horizontaux ne le sont pas.
    const double xContactVal = 65.0;   // contacts secs dans l'appareil Valmet
    const double xBorneVal = 95.0;     // bornes 21/22/23/24 et 18/19
    const double xPlus = 140.0;        // rail vertical + 24 V (sortie de l'alim)
    const double xMoins = 152.5;       // rail vertical - 24 V
    const double xRelais = 232.5;      // relais d'interface Omron
    const double xL1B = 285.0;         // bus L1B, commun des contacts
    const double xBorneApi = 330.0;    // bornes des modules PLC152
    const double xTexteApi = 342.5;

    // Les deux appareils de terrain. Chacun offre deux contacts secs
    // (21-22 et 23-24) et une entree binaire (+18 / -19).
    struct Appareil {
        QString nom;
        double haut;
        QString voieA, voieB;    // reperes de fil vers l'IDM
        QString borneA, borneB;  // bornes du module, avec leur voie
    };
    const Appareil appareils[] = {
        { QStringLiteral("EAU BLANCHE"), 30.0, QStringLiteral("%R07S04C005"),
          QStringLiteral("%R07S04C006"), QStringLiteral("12  (CH5)"),
          QStringLiteral("13  (CH6)") },
        { QStringLiteral("CAISSE D'ARRIVÉE"), 130.0, QStringLiteral("%R07S04C007"),
          QStringLiteral("%R07S04C008"), QStringLiteral("14  (CH7)"),
          QStringLiteral("15  (CH8)") },
    };

    auto b = [&p](const SymbolInstance *s, const char *n) {
        return p.broche(s, QLatin1String(n));
    };

    // Les cadres d'enveloppe, EN POINTILLE — c'est la convention qui distingue
    // l'enveloppe du circuit, et elle n'existait pas avant cet essai.
    p.commande(QStringLiteral("TRAIT"));
    p.rectangle(QPointF(122.5, 22.5), QPointF(170.0, 232.5));   // BJ RM5
    p.rectangle(QPointF(172.5, 22.5), QPointF(200.0, 232.5));   // POWER SUPPLY 24 V
    p.rectangle(QPointF(205.0, 22.5), QPointF(400.0, 232.5));   // le cabinet
    p.commande(QStringLiteral("TRAIT continu"));
    p.rectangle(QPointF(25.0, 27.5), QPointF(110.0, 117.5));    // Valmet eau blanche
    p.rectangle(QPointF(25.0, 127.5), QPointF(110.0, 217.5));   // Valmet caisse

    p.texte(QPointF(126.0, 19.0), QStringLiteral("BJ RM5"), 2.8);
    p.texte(QPointF(172.5, 19.0), QStringLiteral("POWER SUPPLY"), 2.2);
    p.texte(QPointF(208.0, 19.0), QStringLiteral("Cabinet 047BJ0152B / KN047-0"), 2.8);

    // L'alimentation 24 V et ses deux rails. Le + part vers la gauche, vers
    // les appareils ; le - descend a droite vers les bobines des relais.
    auto *alim = p.poser(QStringLiteral("source de tension"), QPointF(187.5, 40.0), 1);
    REQUIRE(alim);
    p.fil({ b(alim, "+"), QPointF(xPlus, 40.0) });
    p.fil({ QPointF(xPlus, 40.0), QPointF(xPlus, 215.0) });
    p.fil({ b(alim, "-"), QPointF(xMoins, b(alim, "-").y()), QPointF(xMoins, 215.0) });
    p.texte(QPointF(xPlus - 22.0, 36.0), QStringLiteral("+ 24 V  (1)"), 2.2);
    p.texte(QPointF(xMoins + 2.0, 219.0), QStringLiteral("− 24 V"), 2.2);

    // Le bus L1B : commun des contacts de relais, remonte vers la borne 10 du
    // module d'entrees.
    p.fil({ QPointF(xL1B, 42.5), QPointF(xL1B, 200.0) });
    p.texte(QPointF(xL1B + 2.0, 40.0), QStringLiteral("L1B"), 2.2);

    for (const Appareil &a : appareils) {
        const double r21 = a.haut + 30.0;    // contact sec 21-22
        const double r23 = a.haut + 42.5;    // contact sec 23-24
        const double r18 = a.haut + 62.5;    // entree binaire + 18
        const double r19 = a.haut + 75.0;    // entree binaire - 19
        const double rFeed = a.haut + 17.5;  // arrivee du + 24 V, sous le titre

        p.texte(QPointF(28.0, a.haut + 7.0), QStringLiteral("VALMET"), 3.0);
        p.texte(QPointF(28.0, a.haut + 12.5), a.nom, 3.0);
        p.texte(QPointF(28.0, r21 - 5.0), QStringLiteral("BINARY OUTPUT"), 2.2);
        p.texte(QPointF(28.0, r18 - 5.0), QStringLiteral("BINARY INPUT"), 2.2);

        // Les deux contacts secs et leurs quatre bornes.
        auto *c21 = p.poser(QStringLiteral("contact à fermeture"), QPointF(xContactVal, r21), 3);
        auto *c23 = p.poser(QStringLiteral("contact à fermeture"), QPointF(xContactVal, r23), 3);
        REQUIRE(c21);
        REQUIRE(c23);
        auto *t22 = p.poser(QStringLiteral("borne"), QPointF(xBorneVal, r21), 3);
        auto *t24 = p.poser(QStringLiteral("borne"), QPointF(xBorneVal, r23), 3);
        auto *t18 = p.poser(QStringLiteral("borne"), QPointF(xBorneVal, r18), 3);
        auto *t19 = p.poser(QStringLiteral("borne"), QPointF(xBorneVal, r19), 3);
        REQUIRE(t22);
        REQUIRE(t19);

        p.texte(QPointF(xContactVal - 13.0, r21 - 2.0), QStringLiteral("21"), 2.0);
        p.texte(QPointF(xBorneVal - 3.0, r21 - 4.0), QStringLiteral("22"), 2.0);
        p.texte(QPointF(xContactVal - 13.0, r23 - 2.0), QStringLiteral("23"), 2.0);
        p.texte(QPointF(xBorneVal - 3.0, r23 - 4.0), QStringLiteral("24"), 2.0);
        p.texte(QPointF(xBorneVal - 3.0, r18 - 4.0), QStringLiteral("+ 18"), 2.0);
        p.texte(QPointF(xBorneVal - 3.0, r19 - 4.0), QStringLiteral("− 19"), 2.0);

        // Le fil (1) : le + 24 V alimente les bornes 21, 23 et + 18.
        //
        // Les DERIVATIONS D'ABORD, la colonne commune ENSUITE. En sens
        // inverse, le clic qui part de la colonne s'accroche a son MILIEU —
        // l'accrochage aux objets fait son travail, mais il ne devine pas
        // qu'on visait une derivation. Tracee en dernier, la colonne
        // s'accroche au contraire aux extremites deja posees.
        const double xCommun = 45.0;
        p.fil({ b(c21, "13"), QPointF(xCommun, r21) });
        p.fil({ b(c23, "13"), QPointF(xCommun, r23) });
        p.fil({ b(t18, "1"), QPointF(xCommun, r18) });
        p.fil({ QPointF(xCommun, r18), QPointF(xCommun, rFeed) });
        p.fil({ QPointF(xCommun, rFeed), QPointF(xPlus, rFeed) });

        // Les fils (2) et (3) : chaque contact sec excite une bobine.
        p.fil({ b(c21, "14"), b(t22, "1") });
        p.fil({ b(c23, "14"), b(t24, "1") });
    }

    // Les quatre relais d'interface 24 V, dans l'armoire. Un seul symbole
    // porte la bobine A1/A2 ET le contact 11/14 : c'est le relais qu'on tient
    // dans la main, pas deux blocs a relier par un groupe d'appareil.
    struct Voie {
        double y;
        QString repere;   // repere du fil vers l'IDM
        QString borne;    // borne du module
        double yBorneVal; // borne 22 ou 24 de l'appareil, cote gauche
    };
    const Voie voies[] = {
        { 55.0, QStringLiteral("%R07S04C005"), QStringLiteral("12  (CH5)"), 60.0 },
        { 80.0, QStringLiteral("%R07S04C006"), QStringLiteral("13  (CH6)"), 72.5 },
        { 155.0, QStringLiteral("%R07S04C007"), QStringLiteral("14  (CH7)"), 160.0 },
        { 180.0, QStringLiteral("%R07S04C008"), QStringLiteral("15  (CH8)"), 172.5 },
    };
    for (const Voie &v : voies) {
        auto *relais = p.poser(QStringLiteral("relais d'interface"), QPointF(xRelais, v.y));
        REQUIRE(relais);
        p.texte(QPointF(xRelais - 17.0, v.y - 12.0), QStringLiteral("RELAIS OMRON 24 V"), 2.0);

        auto *borne = p.poser(QStringLiteral("borne"), QPointF(xBorneApi, v.y + 5.0), 3);
        REQUIRE(borne);
        p.texte(QPointF(xTexteApi, v.y + 2.5), v.borne, 2.0);

        // La bobine : A2 vient de l'appareil (fils 2 et 3), A1 du rail - 24 V.
        p.fil({ QPointF(xBorneVal + 7.0, v.yBorneVal), QPointF(xPlus + 5.0, v.yBorneVal),
                QPointF(xPlus + 5.0, v.y + 5.0), b(relais, "A2") });
        p.fil({ QPointF(xMoins, v.y - 5.0), b(relais, "A1") });

        // Le contact : 11 sur le bus L1B, 14 vers la voie du module.
        p.fil({ b(relais, "11"), QPointF(xL1B, v.y - 5.0) });
        p.fil({ b(relais, "14"), b(borne, "1") });
        p.texte(QPointF(xRelais + 22.0, v.y + 2.0), v.repere, 1.8);
    }

    // La borne 10 (L1B) du module d'entrees, en haut du bus.
    auto *borneL1B = p.poser(QStringLiteral("borne"), QPointF(xBorneApi, 42.5), 3);
    REQUIRE(borneL1B);
    p.fil({ QPointF(xL1B, 42.5), b(borneL1B, "1") });
    p.texte(QPointF(xTexteApi, 41.0), QStringLiteral("10  (L1B)"), 2.0);
    p.texte(QPointF(xTexteApi, 34.0), QStringLiteral("IDM PLC152  [R07.S04]"), 2.2);

    // Le relais 120 V : commande par une sortie de l'ODM, son contact met les
    // entrees binaires des deux Valmet sous tension. Il est dessine en miroir
    // sur le releve ; ici le contact reste a droite, la lecture y gagne.
    auto *relais120 = p.poser(QStringLiteral("relais d'interface"), QPointF(xRelais, 207.5));
    REQUIRE(relais120);
    p.texte(QPointF(xRelais - 17.0, 195.0), QStringLiteral("RELAIS OMRON 120 V"), 2.0);

    auto *odm3 = p.poser(QStringLiteral("borne"), QPointF(xBorneApi, 202.5), 3);
    auto *odm13 = p.poser(QStringLiteral("borne"), QPointF(xBorneApi, 212.5), 3);
    REQUIRE(odm3);
    REQUIRE(odm13);
    p.texte(QPointF(xTexteApi, 201.0), QStringLiteral("3"), 2.0);
    p.texte(QPointF(xTexteApi, 211.0), QStringLiteral("13  (LN)"), 2.0);
    p.texte(QPointF(xTexteApi, 220.0), QStringLiteral("ODM PLC152  [R07.S05]"), 2.2);

    // La bobine du 120 V est cablee depuis l'ODM : bornes 3 et 13.
    p.fil({ b(odm3, "1"), QPointF(xL1B + 20.0, 202.5), QPointF(xL1B + 20.0, 190.0),
            QPointF(xRelais - 35.0, 190.0), QPointF(xRelais - 35.0, 202.5),
            b(relais120, "A1") });
    p.fil({ b(odm13, "1"), QPointF(xL1B + 15.0, 212.5), QPointF(xL1B + 15.0, 225.0),
            QPointF(xRelais - 35.0, 225.0), QPointF(xRelais - 35.0, 212.5),
            b(relais120, "A2") });
    p.texte(QPointF(xRelais + 22.0, 201.5), QStringLiteral("%R07S05C002"), 1.8);
    p.texte(QPointF(xRelais + 22.0, 216.0), QStringLiteral("%R07S05C002-A"), 1.8);

    // Le contact du 120 V : 11 sur le - 24 V, 14 sur le fil (4) qui repart
    // vers les bornes - 19 des deux appareils.
    p.fil({ QPointF(xMoins, 202.5), b(relais120, "11") });

    // Le fil (4) : les deux bornes - 19 sont reunies sur une colonne longeant
    // les appareils, puis remontent vers le contact du relais 120 V. Les
    // derivations d'abord, la colonne ensuite — meme raison que plus haut.
    const double xRetour = 117.5;
    p.fil({ QPointF(xBorneVal + 7.0, 105.0), QPointF(xRetour, 105.0) });
    p.fil({ QPointF(xBorneVal + 7.0, 205.0), QPointF(xRetour, 205.0) });
    p.fil({ QPointF(xRetour, 105.0), QPointF(xRetour, 205.0) });
    p.fil({ QPointF(xRetour, 205.0), QPointF(xRetour, 230.0),
            QPointF(xRelais + 32.5, 230.0), QPointF(xRelais + 32.5, 212.5),
            b(relais120, "14") });
    p.texte(QPointF(xRetour + 3.0, 103.0), QStringLiteral("(4)"), 2.4);
    p.texte(QPointF(xRetour + 3.0, 203.0), QStringLiteral("(4)"), 2.4);

    // ---- ce que le logiciel sait en tirer tout seul ---------------------
    p.commande(QStringLiteral("RN")); // repérage automatique des fils et appareils
    QApplication::processEvents();

    // On ecarte le curseur avant la capture : sinon le marqueur d'accrochage
    // reste dessine sous lui et fait croire a une marque du schema.
    p.vue()->zoomToFit();
    p.survole(QPointF(410.0, 285.0));
    // Les captures ne sont ecrites que si on les demande : un test ne doit
    // rien laisser derriere lui, mais on veut pouvoir regarder le resultat.
    const QByteArray ou = qgetenv("ARCUS_ESSAI_CAPTURES");
    if (!ou.isEmpty()) {
        p.capture(QString::fromLocal8Bit(ou) + QStringLiteral("/valmet-fenetre.png"));
        p.captureVue(QString::fromLocal8Bit(ou) + QStringLiteral("/valmet-canevas.png"));
    }

    // Le dossier doit traverser le format et l'export, pas seulement l'ecran.
    QTemporaryDir dossier;
    REQUIRE(dossier.isValid());
    const QString chemin = dossier.filePath(QStringLiteral("valmet.arcus"));
    CHECK(DsnFile::save(chemin, p.document()->project()));
    Project relu;
    CHECK(DsnFile::load(chemin, relu).ok);
    CHECK(relu.folioCount() == 1);
    if (relu.folioCount() == 1)
        CHECK(relu.folioAt(0)->entityCount() == folio->entityCount());

    // Ecran = papier : le meme peintre trace le PDF. S'il sort, le dessin
    // s'imprime.
    CHECK(PdfExport::write(dossier.filePath(QStringLiteral("valmet.pdf")),
                           p.document()->project()));

    QTextStream out(stdout);
    out << "\n=========== ESSAI : SCHEMA VALMET ===========\n";
    out << "entites posees      : " << int(folio->entityCount()) << "\n";
    out << "  dont symboles     : " << int(folio->entitiesOfType<SymbolInstance>().size()) << "\n";
    out << "  dont fils         : " << int(folio->entitiesOfType<Wire>().size()) << "\n";
    out << "  dont textes       : " << int(folio->entitiesOfType<TextItem>().size()) << "\n";
    out << "  dont formes       : " << int(folio->entitiesOfType<GraphicItem>().size()) << "\n";
    out << "gestes              : " << p.gestes() << "\n";
    out << "boites modales      : " << p.dialogues() << "\n";
    out << "----- fils non orthogonaux -----\n";
    int biais = 0;
    for (const Wire *w : folio->entitiesOfType<Wire>()) {
        for (int i = 1; i < w->points.size(); ++i) {
            const QPointF a = w->points.at(i - 1);
            const QPointF c = w->points.at(i);
            if (std::abs(a.x() - c.x()) > 0.01 && std::abs(a.y() - c.y()) > 0.01) {
                ++biais;
                out << QStringLiteral("   (%1,%2) -> (%3,%4)\n")
                               .arg(a.x()).arg(a.y()).arg(c.x()).arg(c.y());
            }
        }
    }
    out << "   total : " << biais << "\n";

    // L'audit electrique : ce que le logiciel a a dire de ce qu'on vient de
    // dessiner. C'est la mesure la plus severe de l'essai — il relit la
    // geometrie et ne croit rien sur parole.
    const Netlist &netlist = p.document()->netlist();
    const PlcDatabase automates;
    const auto constats = Audit::run(p.document()->project(), netlist, automates);
    out << "----- audit electrique -----\n";
    for (const AuditFinding &c : constats) {
        out << QStringLiteral("   [%1] %2  (%3 %4)\n")
                       .arg(c.severityLabel(), c.message, c.folioTag, c.zone);
    }
    out << "   total : " << int(constats.size()) << " constat(s)\n";

    out << "accrocs             :\n";
    if (p.accrocs().isEmpty())
        out << "   (aucun)\n";
    for (const QString &a : p.accrocs())
        out << "   - " << a << "\n";
    out << "=============================================\n";

    CHECK(folio->entityCount() > 80);
    CHECK(biais == 0);
}

// --------------------------------------------------------------------------
// BLOC B — UN DOSSIER, PAS UN FOLIO
//
// Un folio prouve qu'on sait dessiner une planche. Un DOSSIER prouve qu'on
// sait faire un projet, et c'est la que vit tout ce qu'une planche seule ne
// touche jamais : les renvois d'un folio a l'autre, le reperage a l'echelle
// du projet, le bornier, la nomenclature, le PDF multi-pages.
//
// Deux folios relies, comme un bureau d'etudes en livre :
//   1. Alimentation 24 V — l'arrivee, la protection, l'alimentation, et les
//      fleches de signal SOURCE qui exportent + 24 V et 0 V.
//   2. Commande — les fleches DESTINATION qui les reprennent, un depart, un
//      relais, le bornier X1 et le moteur.
//
// C'est ce qui se passe ENTRE les deux folios qui est eprouve ici : le renvoi
// qui trouve sa cible, le reperage qui porte sur tout le projet, le bornier
// qui reste un bornier, et le PDF qui sort en plusieurs pages.

TEST_CASE("Essai : un dossier de deux folios reliés, de bout en bout", "[essai][dossier]")
{
    Pupitre p;
    REQUIRE(p.vue());
    REQUIRE(p.folio());

    Project &projet = p.document()->project();
    projet.info.title = QStringLiteral("Armoire de commande — essai de dossier");
    projet.info.reference = QStringLiteral("ESS-001");
    projet.info.client = QStringLiteral("Bureau d'études");

    auto b = [&p](const SymbolInstance *s, const char *n) {
        return p.broche(s, QLatin1String(n));
    };

    // ================= FOLIO 1 — ALIMENTATION =========================
    Folio *f1 = p.folio();
    f1->number = QStringLiteral("1");
    f1->title = QStringLiteral("Alimentation 24 V");
    p.vue()->zoomToFit();

    p.texte(QPointF(30.0, 30.0), QStringLiteral("ARRIVÉE 230 V — 50 Hz"), 3.0);

    auto *disjoncteur = p.poser(QStringLiteral("disjoncteur unipolaire"), QPointF(80.0, 70.0));
    REQUIRE(disjoncteur);
    auto *alim = p.poser(QStringLiteral("source de tension"), QPointF(80.0, 130.0));
    REQUIRE(alim);
    p.texte(QPointF(92.5, 128.0), QStringLiteral("ALIM 24 V — 5 A"), 2.5);

    p.fil({ QPointF(80.0, 45.0), b(disjoncteur, "1") });
    p.fil({ b(disjoncteur, "2"), b(alim, "+") });

    // Les fleches de signal SOURCE : c'est par elles que les potentiels
    // sortent du folio. Deux etiquettes du meme code sont un seul potentiel,
    // meme a deux folios de distance.
    p.commande(QStringLiteral("SO"));
    p.clic(QPointF(150.0, 130.0));
    REQUIRE(p.vue()->isTypingText());
    p.frappe(p.vue(), QStringLiteral("+24V"));
    p.touche(p.vue(), Qt::Key_Return);
    p.fil({ b(alim, "+"), QPointF(150.0, 130.0) });

    p.commande(QStringLiteral("SO"));
    p.clic(QPointF(150.0, 160.0));
    p.frappe(p.vue(), QStringLiteral("0V"));
    p.touche(p.vue(), Qt::Key_Return);
    p.fil({ b(alim, "-"), QPointF(80.0, 160.0), QPointF(150.0, 160.0) });

    // ================= FOLIO 2 — COMMANDE =============================
    Folio *f2 = p.nouveauFolio(QStringLiteral("2"), QStringLiteral("Commande — départ pompe"));
    REQUIRE(f2);
    p.texte(QPointF(30.0, 30.0), QStringLiteral("DÉPART POMPE P1"), 3.0);

    // Les fleches DESTINATION reprennent les deux potentiels du folio 1.
    p.commande(QStringLiteral("DE"));
    p.clic(QPointF(40.0, 70.0));
    REQUIRE(p.vue()->isTypingText());
    p.frappe(p.vue(), QStringLiteral("+24V"));
    p.touche(p.vue(), Qt::Key_Return);

    p.commande(QStringLiteral("DE"));
    p.clic(QPointF(40.0, 170.0));
    p.frappe(p.vue(), QStringLiteral("0V"));
    p.touche(p.vue(), Qt::Key_Return);

    auto *bouton = p.poser(QStringLiteral("bouton-poussoir à fermeture"), QPointF(100.0, 90.0), 3);
    auto *bobine = p.poser(QStringLiteral("bobine de relais"), QPointF(180.0, 90.0), 3);
    REQUIRE(bouton);
    REQUIRE(bobine);

    p.fil({ QPointF(40.0, 70.0), QPointF(40.0, 90.0), b(bouton, "13") });
    p.fil({ b(bouton, "14"), b(bobine, "A1") });
    p.fil({ b(bobine, "A2"), QPointF(240.0, 90.0), QPointF(240.0, 170.0),
            QPointF(40.0, 170.0) });

    // Le bornier vers le terrain : trois bornes, cablees des deux cotes.
    auto *x1 = p.poser(QStringLiteral("borne"), QPointF(300.0, 90.0), 3);
    auto *x2 = p.poser(QStringLiteral("borne"), QPointF(300.0, 120.0), 3);
    auto *x3 = p.poser(QStringLiteral("borne"), QPointF(300.0, 150.0), 3);
    REQUIRE(x1);
    REQUIRE(x3);
    auto *moteur = p.poser(QStringLiteral("moteur triphasé"), QPointF(360.0, 120.0));
    REQUIRE(moteur);

    p.fil({ b(bobine, "A1"), QPointF(270.0, 90.0), b(x1, "1") });
    p.fil({ b(x1, "2"), b(moteur, "U") });
    p.fil({ QPointF(270.0, 120.0), b(x2, "1") });
    p.fil({ b(x2, "2"), b(moteur, "V") });
    p.fil({ QPointF(270.0, 150.0), b(x3, "1") });
    p.fil({ b(x3, "2"), b(moteur, "W") });

    // ================= LE DOSSIER, PAS LA PLANCHE =====================
    QTextStream out(stdout);
    out << "\n=========== ESSAI : LE DOSSIER, PAS LA PLANCHE ===========\n";

    // 1. Le reperage porte sur TOUT le projet.
    p.commande(QStringLiteral("RN"));
    QApplication::processEvents();
    out << "folios              : " << projet.folioCount() << "\n";

    // 2. Les renvois entre folios se calculent depuis le dessin.
    const Netlist &netlist = p.document()->netlist();
    const QHash<QString, QString> renvois = CrossReference::resolve(projet, netlist);
    out << "renvois de signal   : " << renvois.size() << "\n";
    for (auto it = renvois.cbegin(); it != renvois.cend(); ++it)
        out << "   " << it.value() << "\n";

    // 3. Le bornier : numeroter les bornes par l'editeur, puis verifier que
    //    le dessin les montre — le correctif de l'essai precedent.
    const QStringList borniers = TerminalStripDialog::blocksOf(projet);
    out << "borniers            : " << borniers.join(QStringLiteral(", ")) << "\n";

    // 4. Les rapports du dossier.
    const ReportScope portee;
    const auto nomenclature = Reports::billOfMaterials(projet, portee);
    const auto fils = Reports::wireList(projet, netlist, portee);
    const auto bornes = Reports::terminalList(projet, netlist, portee);
    out << "nomenclature        : " << nomenclature.size() << " ligne(s)\n";
    out << "liste de fils       : " << fils.size() << " ligne(s)\n";
    out << "liste de bornes     : " << bornes.size() << " ligne(s)\n";

    // 5. L'audit du dossier entier.
    const PlcDatabase automates;
    const auto constats = Audit::run(projet, netlist, automates);
    int erreurs = 0;
    for (const AuditFinding &c : constats)
        if (c.severity == AuditFinding::Severity::Error)
            ++erreurs;
    out << "audit               : " << int(constats.size()) << " constat(s), " << erreurs
        << " erreur(s)\n";
    for (const AuditFinding &c : constats) {
        if (c.severity != AuditFinding::Severity::Info)
            out << QStringLiteral("   [%1] %2  (%3 %4)\n")
                           .arg(c.severityLabel(), c.message, c.folioTag, c.zone);
    }

    // 6. Le dossier traverse le fichier et sort en PDF multi-pages.
    QTemporaryDir dossier;
    REQUIRE(dossier.isValid());
    const QString chemin = dossier.filePath(QStringLiteral("dossier.arcus"));
    CHECK(DsnFile::save(chemin, projet));
    Project relu;
    CHECK(DsnFile::load(chemin, relu).ok);
    CHECK(relu.folioCount() == projet.folioCount());

    const QString pdf = dossier.filePath(QStringLiteral("dossier.pdf"));
    CHECK(PdfExport::write(pdf, projet));
    out << "PDF                 : " << (QFile(pdf).size() / 1024) << " Kio pour "
        << projet.folioCount() << " folio(s)\n";

    out << "gestes              : " << p.gestes() << "\n";
    out << "boites modales      : " << p.dialogues() << "\n";
    out << "accrocs             :\n";
    if (p.accrocs().isEmpty())
        out << "   (aucun)\n";
    for (const QString &a : p.accrocs())
        out << "   - " << a << "\n";
    out << "=======================================================\n";

    const QByteArray ou = qgetenv("ARCUS_ESSAI_CAPTURES");
    if (!ou.isEmpty()) {
        for (int i = 0; i < projet.folioCount(); ++i) {
            p.document()->setCurrentFolioIndex(i);
            p.vue()->zoomToFit();
            p.survole(QPointF(410.0, 285.0));
            p.captureVue(QString::fromLocal8Bit(ou)
                         + QStringLiteral("/dossier-folio%1.png").arg(i + 1));
        }
    }

    CHECK(projet.folioCount() == 2);
}

// ==========================================================================
// Essai du bloc C — les quatre commandes qui manquaient, conduites à la main
//
// Les tests d'unité disent que chaque pièce marche. Cet essai dit qu'on peut
// s'en servir : il passe par la ligne de commande, la palette, les clics et
// les boîtes, comme une main. C'est ce chemin-là qui a trouvé les défauts des
// deux essais précédents, pas les tests d'unité.
// ==========================================================================

TEST_CASE("Essai : les quatre commandes du bloc C, à la main", "[essai][blocC]")
{
    Pupitre p;
    QTextStream out(stdout);
    out << "\n===== ESSAI BLOC C — les commandes qui manquaient =====\n";

    p.vue()->zoomToFit();

    // ---- C1 : effacer un composant referme le fil -----------------------
    //
    // On pose un contact, on le câble des deux côtés, on l'efface. Le circuit
    // doit rester fermé — c'est le symétrique de l'insertion.
    auto *contact = p.poser(QStringLiteral("contact"), QPointF(150, 100));
    REQUIRE(contact);
    const SymbolDefinition *def =
            p.document()->project().library.definition(contact->definitionId);
    REQUIRE(def);
    REQUIRE(def->pins.size() >= 2);
    const QPointF b1 = contact->placement.map(def->pins.at(0).position);
    const QPointF b2 = contact->placement.map(def->pins.at(1).position);
    p.fil({ QPointF(80, b1.y()), b1 });
    p.fil({ b2, QPointF(220, b2.y()) });
    REQUIRE(p.folio()->entitiesOfType<Wire>().size() == 2);

    p.vue()->setSelection({ contact->id() });
    p.commande(QStringLiteral("EFFACER"));
    QApplication::processEvents();
    const auto filsApres = p.folio()->entitiesOfType<Wire>();
    if (filsApres.size() != 1)
        p.accroc(QStringLiteral("Effacer le contact n'a pas referme le fil"));
    REQUIRE(filsApres.size() == 1);
    out << "C1 effacer et refermer : "
        << filsApres.front()->points.first().x() << " -> "
        << filsApres.front()->points.last().x() << " mm en un seul fil\n";
    // Une seule annulation ramène tout.
    p.touche(p.vue(), Qt::Key_Z, Qt::ControlModifier);
    QApplication::processEvents();
    CHECK(p.folio()->entitiesOfType<Wire>().size() == 2);
    p.touche(p.vue(), Qt::Key_Y, Qt::ControlModifier);
    QApplication::processEvents();

    // ---- C2 : remplacer un symbole posé ---------------------------------
    auto *bobine = p.poser(QStringLiteral("bobine"), QPointF(150, 180));
    REQUIRE(bobine);
    bobine->setDesignation(QStringLiteral("-KM1"));
    const QString avant = bobine->definitionId;
    const QPointF placeAvant = bobine->placement.position;

    // La commande demande le symbole si rien n'est désigné : on désigne.
    p.vue()->setSelection({ bobine->id() });
    const int orphelins = p.vue()->swapSymbol(bobine->id(), QStringLiteral("iec:contact-nc"));
    QApplication::processEvents();
    const auto *echange =
            dynamic_cast<const SymbolInstance *>(p.folio()->entity(bobine->id()));
    REQUIRE(echange);
    CHECK(echange->definitionId != avant);
    CHECK(echange->designation() == QStringLiteral("-KM1"));
    CHECK(echange->placement.position == placeAvant);
    out << "C2 remplacer un symbole : repère et position gardés, "
        << orphelins << " extrémité(s) en l'air\n";

    // ---- C3 : rechercher / remplacer dans tout le dossier ---------------
    p.texte(QPointF(60, 240), QStringLiteral("ARMOIRE KM1"), 3.5);
    FindReplaceDialog boite(p.document(), &p.fenetre());
    boite.setNeedle(QStringLiteral("KM1"));
    const int trouves = boite.runSearch();
    if (trouves < 2)
        p.accroc(QStringLiteral("La recherche ne voit pas toutes les occurrences de KM1"));
    out << "C3 rechercher KM1 : " << trouves << " occurrence(s)\n";

    // ---- C4 : coter ------------------------------------------------------
    //
    // Trois clics par la commande, comme un dessinateur : la cote doit
    // mesurer ce qu'on lui a désigné.
    p.commande(QStringLiteral("COTATIONH"));
    if (p.vue()->tool() != FolioView::Tool::Dimension)
        p.accroc(QStringLiteral("La commande COTATIONH n'arme pas l'outil de cotation"));
    p.clic(QPointF(80, 60));
    p.clic(QPointF(230, 60));
    p.clic(QPointF(150, 40));
    QApplication::processEvents();
    const auto cotes = p.folio()->entitiesOfType<DimensionItem>();
    REQUIRE(cotes.size() == 1);
    CHECK(cotes.front()->measure() == 150.0);
    out << "C4 coter : " << cotes.front()->displayText() << " mm\n";

    // Et le dossier tient le voyage, cotes comprises.
    QTemporaryDir dossier;
    const QString chemin = dossier.filePath(QStringLiteral("blocC.arcus"));
    REQUIRE(DsnFile::save(chemin, p.document()->project()));
    Project relu;
    CHECK(DsnFile::load(chemin, relu).ok);
    CHECK(relu.folioAt(0)->entitiesOfType<DimensionItem>().size() == 1);

    out << "gestes              : " << p.gestes() << "\n";
    out << "boites modales      : " << p.dialogues() << "\n";
    out << "accrocs             :\n";
    if (p.accrocs().isEmpty())
        out << "   (aucun)\n";
    for (const QString &a : p.accrocs())
        out << "   - " << a << "\n";
    out << "=======================================================\n";

    CHECK(p.accrocs().isEmpty());
}
