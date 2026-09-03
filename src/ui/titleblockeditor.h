// L'editeur de cartouche — « nous devons pouvoir créer une cartouche perso ».
//
// Le cartouche est ce qu'un bureau d'etudes regarde en premier, et le seul
// endroit du dessin qui porte SON identite. Un logiciel qui n'en offre qu'un,
// figé dans le code, ne sort pas d'un essai : le dossier tiré avec le
// cartouche d'un autre ne part pas au chantier.
//
// Quatre decisions :
//
// 1. **L'apercu est peint par le VRAI peintre** (`FolioPainter::paintTitleBlock`)
//    sur un projet d'exemple. Redessiner l'apercu autrement rouvrirait l'ecart
//    entre ce qu'on regle et ce qui s'imprime — l'ecart que tout ce logiciel
//    existe pour fermer.
// 2. **On deplace les cases a la souris**, on ne tape pas des coordonnees.
//    Composer un cartouche est un travail d'oeil ; le formulaire reste, pour
//    poser un chiffre exact quand on le veut.
// 3. **Une case ne connait qu'une CLEF de champ**, choisie dans une liste. La
//    liste vient du coeur (`TitleBlock::fieldCaptions`) : ecrire les clefs a
//    la main ici les ferait diverger du jour ou l'on ajoute un champ.
// 4. **Rien n'est applique tant qu'on n'a pas validé**, et l'application
//    passe par une commande annulable. Se tromper de cartouche sur un dossier
//    de quarante planches doit se defaire d'un Ctrl+Z.
#pragma once

#include "core/project.h"
#include "core/titleblock.h"

#include <QByteArray>
#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace dsn {

class Document;
class TitleBlockPreview;

class TitleBlockEditor : public QDialog
{
    Q_OBJECT

public:
    explicit TitleBlockEditor(Document *document, QWidget *parent = nullptr);

    // Le gabarit tel qu'il est dans la boite. Un test le lit sans passer par
    // les widgets — ce sont les regles qui portent le comportement.
    const TitleBlockTemplate &edited() const { return m_template; }
    const QMap<QString, QByteArray> &images() const { return m_images; }
    void setTemplate(const TitleBlockTemplate &gabarit);

    int selectedCell() const { return m_current; }
    void selectCell(int index);

    // Applique au document, en une commande annulable. Appele par « OK », et
    // par les tests.
    void apply();

private:
    void buildCellList();
    void showCell(int index);
    void pullFromForm();
    void addCell(TitleBlockCell::Kind kind);
    void removeCell();
    void loadImage(const QString &key);
    void refresh();

    Document *m_document = nullptr;
    // Une COPIE du projet, pour l'apercu : il doit montrer le cartouche avec
    // les vraies valeurs du dossier sans risquer d'y toucher pendant qu'on
    // regle. Rien n'entre dans le document avant « Appliquer ».
    Project m_modelProject;
    TitleBlockTemplate m_template;
    QMap<QString, QByteArray> m_images;
    int m_current = -1;
    bool m_filling = false;

    TitleBlockPreview *m_preview = nullptr;
    QComboBox *m_builtins = nullptr;
    QLineEdit *m_name = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QListWidget *m_cells = nullptr;

    QComboBox *m_kind = nullptr;
    QDoubleSpinBox *m_x = nullptr;
    QDoubleSpinBox *m_y = nullptr;
    QDoubleSpinBox *m_w = nullptr;
    QDoubleSpinBox *m_h = nullptr;
    QLineEdit *m_label = nullptr;
    QComboBox *m_field = nullptr;
    QLineEdit *m_text = nullptr;
    QLineEdit *m_columns = nullptr;
    QDoubleSpinBox *m_textHeight = nullptr;
    QDoubleSpinBox *m_labelHeight = nullptr;
    QComboBox *m_layout = nullptr;
    QComboBox *m_align = nullptr;
    QCheckBox *m_border = nullptr;
};

} // namespace dsn
