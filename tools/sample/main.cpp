// Genere le projet d'exemple livre avec le logiciel : le demarrage direct d'un
// moteur triphase, en deux folios (puissance et commande).
//
// Cet outil sert aussi de verification de bout en bout : il exerce la
// bibliotheque, la connectivite, le reperage automatique, les rapports, le
// rendu et les trois formats de sortie en une seule passe.
#include "core/entities.h"
#include "core/netlist.h"
#include "core/project.h"
#include "io/csvexport.h"
#include "io/dsnfile.h"
#include "io/dxfexport.h"
#include "render/pdfexport.h"
#include "rules/numbering.h"
#include "rules/reports.h"
#include "symbols/librarystore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>

using namespace dsn;

namespace {

SymbolInstance *place(Project &project, Folio *folio, const QString &definitionId,
                      const QPointF &at, const QString &group = QString(),
                      Orientation orientation = Orientation::R0)
{
    auto instance = std::make_unique<SymbolInstance>();
    instance->definitionId = definitionId;
    instance->placement.position = at;
    instance->placement.orientation = orientation;
    instance->deviceGroup = group;
    if (const SymbolDefinition *definition = project.library.definition(definitionId))
        instance->setLocalBounds(definition->bounds());
    else
        qWarning() << "symbole introuvable :" << definitionId;
    auto *raw = instance.get();
    folio->addEntity(std::move(instance));
    return raw;
}

Wire *wire(Folio *folio, const QVector<QPointF> &points, const QStringList &conductors = {})
{
    auto w = std::make_unique<Wire>();
    w->points = points;
    w->conductors = conductors;
    auto *raw = w.get();
    folio->addEntity(std::move(w));
    return raw;
}

void junction(Folio *folio, const QPointF &at)
{
    auto j = std::make_unique<Junction>();
    j->point = at;
    folio->addEntity(std::move(j));
}

Label *label(Folio *folio, const QPointF &at, const QString &name, Direction direction,
             Label::Scope scope = Label::Scope::Project)
{
    auto l = std::make_unique<Label>();
    l->point = at;
    l->name = name;
    l->direction = direction;
    l->scope = scope;
    auto *raw = l.get();
    folio->addEntity(std::move(l));
    return raw;
}

void note(Folio *folio, const QPointF &at, const QString &text, double height = 3.0)
{
    auto t = std::make_unique<TextItem>();
    t->text = text;
    t->placement.position = at;
    t->height = height;
    folio->addEntity(std::move(t));
}

// --------------------------------------------------------------------------
// Folio 1 — circuit de puissance
//
// Colonne triphasee descendante : arrivee, sectionnement, contacteur,
// protection thermique, moteur. Les trois poles sont espaces de 7,5 mm, ce qui
// est l'entraxe des symboles tripolaires de la bibliotheque.
void buildPowerFolio(Project &project, Folio *folio)
{
    folio->number = QStringLiteral("1");
    folio->title = QStringLiteral("Circuit de puissance");

    constexpr double x = 140.0;
    const double poles[3] = { x - 7.5, x, x + 7.5 };
    const QStringList phases{ QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3") };

    note(folio, QPointF(35, 30), QStringLiteral("Démarrage direct — moteur 1,5 kW"), 4.0);

    // Arrivee du reseau.
    for (int i = 0; i < 3; ++i) {
        label(folio, QPointF(poles[i], 42), phases.at(i), Direction::Up);
        wire(folio, { QPointF(poles[i], 42), QPointF(poles[i], 60) });
    }

    place(project, folio, QStringLiteral("iec:circuit-breaker-3p"), QPointF(x, 70),
          QStringLiteral("Q1"));
    place(project, folio, QStringLiteral("iec:contactor-power-3p"), QPointF(x, 120),
          QStringLiteral("KM1"));
    place(project, folio, QStringLiteral("iec:thermal-relay-3p"), QPointF(x, 170),
          QStringLiteral("F1"));

    for (int i = 0; i < 3; ++i) {
        wire(folio, { QPointF(poles[i], 80), QPointF(poles[i], 110) });  // Q1 -> KM1
        wire(folio, { QPointF(poles[i], 130), QPointF(poles[i], 160) }); // KM1 -> F1
    }

    // Descente vers le moteur : les bornes moteur sont a 5 mm d'entraxe, les
    // poles a 7,5 mm. Le raccordement se resserre en deux coudes.
    place(project, folio, QStringLiteral("iec:motor-3ph"), QPointF(x, 230));
    const double motorPins[3] = { x - 5.0, x, x + 5.0 };
    for (int i = 0; i < 3; ++i) {
        if (i == 1) {
            wire(folio, { QPointF(poles[i], 180), QPointF(poles[i], 217) });
        } else {
            wire(folio, { QPointF(poles[i], 180), QPointF(poles[i], 205),
                          QPointF(motorPins[i], 205), QPointF(motorPins[i], 217) });
        }
    }

    // Terre.
    place(project, folio, QStringLiteral("iec:earth"), QPointF(x, 255));
    wire(folio, { QPointF(x, 243), QPointF(x, 249) });
}

// --------------------------------------------------------------------------
// Folio 2 — circuit de commande
//
// Echelle verticale : phase en haut, neutre en bas, organes en serie. Le
// contact d'auto-maintien est monte en parallele du bouton de marche.
void buildControlFolio(Project &project, Folio *folio)
{
    folio->number = QStringLiteral("2");
    folio->title = QStringLiteral("Circuit de commande");

    constexpr double x = 80.0;
    constexpr double xHold = 110.0; // colonne de l'auto-maintien
    constexpr double xLamp = 145.0;

    note(folio, QPointF(35, 30), QStringLiteral("Commande 230 V — marche / arrêt"), 4.0);

    label(folio, QPointF(x, 40), QStringLiteral("L1"), Direction::Up);
    wire(folio, { QPointF(x, 40), QPointF(x, 50) });

    place(project, folio, QStringLiteral("iec:fuse"), QPointF(x, 60), QStringLiteral("F2"));
    wire(folio, { QPointF(x, 70), QPointF(x, 85) });

    place(project, folio, QStringLiteral("iec:thermal-contact"), QPointF(x, 95),
          QStringLiteral("F1")); // meme appareil que le relais du folio 1
    wire(folio, { QPointF(x, 105), QPointF(x, 120) });

    place(project, folio, QStringLiteral("iec:pushbutton-nc"), QPointF(x, 130),
          QStringLiteral("S1"));
    wire(folio, { QPointF(x, 140), QPointF(x, 160) });

    place(project, folio, QStringLiteral("iec:pushbutton-no"), QPointF(x, 170),
          QStringLiteral("S2"));

    // Auto-maintien : contact KM1 en parallele du bouton de marche.
    place(project, folio, QStringLiteral("iec:contact-no"), QPointF(xHold, 170),
          QStringLiteral("KM1"));
    wire(folio, { QPointF(x, 160), QPointF(xHold, 160) });
    wire(folio, { QPointF(x, 180), QPointF(xHold, 180) });
    junction(folio, QPointF(x, 160));
    junction(folio, QPointF(x, 180));

    wire(folio, { QPointF(x, 180), QPointF(x, 205) });

    place(project, folio, QStringLiteral("iec:coil"), QPointF(x, 215), QStringLiteral("KM1"));

    // Voyant de marche, en parallele de la bobine.
    place(project, folio, QStringLiteral("iec:indicator-lamp"), QPointF(xLamp, 215),
          QStringLiteral("H1"));
    wire(folio, { QPointF(x, 205), QPointF(xLamp, 205) });
    wire(folio, { QPointF(x, 225), QPointF(xLamp, 225) });
    junction(folio, QPointF(x, 205));
    junction(folio, QPointF(x, 225));

    wire(folio, { QPointF(x, 225), QPointF(x, 245) });
    label(folio, QPointF(x, 245), QStringLiteral("N"), Direction::Down);
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Dessins"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const QString outputDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("examples");
    QDir().mkpath(outputDir);

    Project project;
    const LibraryLoadReport report = LibraryStore::loadBuiltin(project.library);
    if (!report.ok()) {
        qWarning() << "bibliotheque :" << report.errors;
        return 1;
    }
    qInfo().noquote() << QStringLiteral("Bibliothèque : %1 symboles").arg(report.symbolsLoaded);

    project.info.title = QStringLiteral("Démarrage direct d'un moteur");
    project.info.client = QStringLiteral("Atelier mécanique Beauport");
    project.info.reference = QStringLiteral("2026-014");
    project.info.author = QStringLiteral("Dessins");
    project.info.revision = QStringLiteral("A");
    project.profileId = QStringLiteral("iec");

    buildPowerFolio(project, project.addFolio());
    buildControlFolio(project, project.addFolio());

    // Reperage automatique : designations puis reperes de fil.
    const Profile profile = Profile::byId(project.profileId);
    const NumberingResult numbering = Numbering::renumberAll(project, profile);
    qInfo().noquote()
            << QStringLiteral("Repérage : %1 appareils, %2 potentiels, %3 fils")
                       .arg(numbering.devicesDesignated)
                       .arg(numbering.netsNumbered)
                       .arg(numbering.wiresNumbered);

    const Netlist netlist = Netlist::build(project);
    qInfo().noquote() << QStringLiteral("Connectivité : %1 potentiels, %2 anomalie(s)")
                                 .arg(netlist.netCount())
                                 .arg(netlist.diagnostics().size());
    for (const auto &d : netlist.diagnostics())
        qInfo().noquote() << "   ·" << d.code << d.message;

    // Verification metier : la bobine et le contact d'auto-maintien du meme
    // contacteur doivent porter la meme designation.
    for (const Folio *folio : project.folios()) {
        for (const SymbolInstance *s : folio->entitiesOfType<SymbolInstance>()) {
            if (s->deviceGroup == QLatin1String("KM1"))
                qInfo().noquote() << QStringLiteral("   KM1 → %1 (%2)")
                                             .arg(s->designation(), s->definitionId);
        }
    }

    QString error;
    const QString dsnPath = QDir(outputDir).filePath(QStringLiteral("demarrage-direct.dsn"));
    if (!DsnFile::save(dsnPath, project, &error)) {
        qWarning() << "ecriture .dsn :" << error;
        return 1;
    }

    const QString pdfPath = QDir(outputDir).filePath(QStringLiteral("demarrage-direct.pdf"));
    if (!PdfExport::write(pdfPath, project, {}, &error)) {
        qWarning() << "ecriture PDF :" << error;
        return 1;
    }

    for (int i = 0; i < project.folioCount(); ++i) {
        const Folio *folio = project.folioAt(i);
        const QString png = QDir(outputDir).filePath(
                QStringLiteral("folio-%1.png").arg(folio->number));
        RenderStyle style = RenderStyle::print();
        style.showPinNumbers = true;
        if (!PdfExport::writePng(png, project, *folio, style, 4.0, &error))
            qWarning() << "ecriture PNG :" << error;
    }

    QStringList dxfErrors;
    DxfExport::writeProject(outputDir, QStringLiteral("demarrage-direct"), project, {},
                            &dxfErrors);
    for (const QString &e : dxfErrors)
        qWarning() << "DXF :" << e;

    CsvExport::write(QDir(outputDir).filePath(QStringLiteral("nomenclature.csv")),
                     Reports::toTable(Reports::billOfMaterials(project)));
    CsvExport::write(QDir(outputDir).filePath(QStringLiteral("liste-fils.csv")),
                     Reports::toTable(Reports::wireList(project, netlist)));

    const ReportTable summary = Reports::projectSummary(project, netlist);
    for (const QStringList &row : summary.rows)
        qInfo().noquote() << QStringLiteral("   %1 : %2").arg(row.at(0), row.at(1));

    qInfo().noquote() << QStringLiteral("Écrit dans %1").arg(QDir(outputDir).absolutePath());
    return 0;
}
