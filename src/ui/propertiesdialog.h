// Les proprietes de la selection, en boite plutot qu'en panneau.
//
// Decision utilisateur (2026-09-02) : le bandeau de droite prenait la place du
// dessin en permanence pour un reglage qu'on ne fait que par moments. Il
// disparait, et le double-clic ouvre la meme chose la ou l'on regarde deja.
//
// La boite n'a PAS de bouton Annuler : le panneau qu'elle porte pousse chaque
// modification comme une commande annulable des la frappe. Un bouton Annuler
// laisserait croire qu'on peut revenir en arriere en fermant, alors que c'est
// Ctrl+Z qui le fait — et lui defait aussi ce qui a ete change avant
// l'ouverture de la boite, ce qui est le comportement juste.
#pragma once

#include <QDialog>
#include <QSet>
#include <QString>

namespace dsn {

class Document;
class PropertiesPanel;

class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    PropertiesDialog(Document *document, const QSet<QString> &selection,
                     QWidget *parent = nullptr);

    // La selection montree. Vide = les proprietes du folio, ce qui est le cas
    // du double-clic dans le vide.
    void setSelection(const QSet<QString> &selection);

private:
    PropertiesPanel *m_panel = nullptr;
};

} // namespace dsn
