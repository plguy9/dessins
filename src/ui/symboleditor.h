// Editeur de symboles.
//
// Le brief identifie la bibliotheque comme le vrai cout du projet : des
// centaines de symboles a dessiner et a verifier, et c'est regulierement ce
// qui enlise ce type de logiciel — le moteur marche, mais il n'y a rien a
// poser dessus. L'editeur arrive donc tot : les symboles deviennent du contenu
// produit en continu, y compris par des non-developpeurs.
#pragma once

#include "core/command.h"
#include "core/symbollibrary.h"
#include "render/renderstyle.h"

#include <QDialog>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QListWidget;
class QScrollArea;

namespace dsn {

// Canevas d'edition. Il travaille sur une copie de la definition : la
// bibliotheque n'est touchee qu'a l'enregistrement.
class SymbolCanvas : public QWidget
{
    Q_OBJECT

public:
    enum class Tool { Select, Line, Polyline, Rect, Circle, Arc, Text, Pin };
    Q_ENUM(Tool)

    // Une selection designe soit une primitive, soit une broche. Un index
    // signe evite deux champs a tenir synchronises.
    static constexpr int kNoSelection = INT_MIN;

    explicit SymbolCanvas(QWidget *parent = nullptr);

    void setDefinition(const SymbolDefinition &definition);
    const SymbolDefinition &definition() const { return m_definition; }

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    int selection() const { return m_selection; }
    void setSelection(int index);
    void deleteSelection();

    void setGridStep(double step);
    double gridStep() const { return m_gridStep; }
    void zoomToFit();

    CommandStack &commands() { return m_commands; }

    // Applique une mutation a la definition en une entree annulable.
    void modify(const QString &text, const std::function<void(SymbolDefinition &)> &mutate);

Q_SIGNALS:
    void definitionChanged();
    void selectionChanged(int index);
    void toolChanged(SymbolCanvas::Tool tool);
    void statusMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPointF toScene(const QPointF &widgetPoint) const;
    QPointF toWidget(const QPointF &scenePoint) const;
    QPointF snap(const QPointF &scenePoint) const;
    int hitTest(const QPointF &scenePoint) const;
    void commitPending();
    void paintPending(QPainter &painter) const;

    SymbolDefinition m_definition;
    CommandStack m_commands;
    Tool m_tool = Tool::Select;
    int m_selection = kNoSelection;

    double m_scale = 8.0; // pixels par millimetre
    QPointF m_pan;
    double m_gridStep = 1.25;
    bool m_fitPending = true;

    QVector<QPointF> m_pending;
    QPointF m_cursor;
    bool m_dragging = false;
    QPointF m_dragLast;
    // Valeur de depart neutre — `RenderStyle()` est exactement ce que rend
    // `screen()`. Le vrai style vient de `MainWindow::buildRenderStyle()`,
    // pose dans le constructeur.
    RenderStyle m_style;
};

class SymbolEditor : public QDialog
{
    Q_OBJECT

public:
    SymbolEditor(SymbolLibrary *library, QWidget *parent = nullptr);

    // Ouvre une definition existante. Un symbole integre est duplique plutot
    // que modifie en place : la bibliotheque livree doit rester reproductible.
    void editDefinition(const QString &definitionId, bool asCopy);
    void newDefinition();

    QString savedDefinitionId() const { return m_savedId; }

private:
    void buildMetadataForm(QFormLayout *form);
    void rebuildElementList();
    void rebuildPropertyForm();
    void applyMetadata();
    void saveDefinition();

    SymbolLibrary *m_library = nullptr;
    SymbolCanvas *m_canvas = nullptr;
    QListWidget *m_elements = nullptr;
    QScrollArea *m_propertyScroll = nullptr;
    QString m_savedId;
    bool m_updating = false;

    QLineEdit *m_name = nullptr;
    QLineEdit *m_logicalId = nullptr;
    QComboBox *m_norm = nullptr;
    QLineEdit *m_category = nullptr;
    QLineEdit *m_prefix = nullptr;
    QLineEdit *m_deviceKind = nullptr;
    QLineEdit *m_keywords = nullptr;
};

} // namespace dsn
