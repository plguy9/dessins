#include "appearance.h"

#include <QColor>
#include <QSettings>
#include <QStringList>

namespace dsn {

namespace {

// Les reglages de forme, communs aux deux themes.
constexpr char kShape[] = "display/";
// Les couleurs, un jeu par theme.
QString colorKey(bool dark, const char *name)
{
    return QStringLiteral("display/") + (dark ? QStringLiteral("dark/") : QStringLiteral("light/"))
            + QString::fromLatin1(name);
}

void loadColor(const QSettings &settings, bool dark, const char *name, QColor &target)
{
    const QVariant value = settings.value(colorKey(dark, name));
    if (!value.isValid())
        return;
    const QColor color(value.toString());
    if (color.isValid())
        target = color;
}

const char *const kColorNames[] = { "crosshair", "grid",  "gridMajor",
                                    "pageBackground", "sheet" };

} // namespace

bool Appearance::darkSheet()
{
    return QSettings().value(QStringLiteral("display/darkSheet"), false).toBool();
}

void Appearance::setDarkSheet(bool on)
{
    QSettings settings;
    if (settings.value(QStringLiteral("display/darkSheet"), false).toBool() == on) {
        settings.setValue(QStringLiteral("display/darkSheet"), on);
        return;
    }

    // CHANGER DE FOND INVALIDE LA COULEUR EPINGLEE. `save()` ecrit la couleur
    // de la feuille et du pourtour a CHAQUE validation de la boite, meme si
    // l'utilisateur n'y a pas touche ; et `load()` passe en dernier, puisque
    // le reglage explicite gagne sur le theme. Sans cet oubli, cocher « fond
    // de dessin sombre » n'aurait plus aucun effet visible des la premiere
    // visite dans les parametres — la couleur de l'autre prereglage
    // resterait posee par-dessus, sans que rien ne le dise.
    //
    // Basculer la preference est un geste explicite qui veut dire « donne-moi
    // l'autre papier » : la teinte retenue contre le prereglage precedent
    // n'a plus de sens.
    for (const char *nom : { "sheet", "pageBackground" }) {
        for (bool dark : { true, false })
            settings.remove(colorKey(dark, nom));
    }
    settings.setValue(QStringLiteral("display/darkSheet"), on);
}

void Appearance::load(RenderStyle &style, bool dark)
{
    const QSettings settings;
    const QString shape = QString::fromLatin1(kShape);

    // Reticule.
    style.showCrosshair =
            settings.value(shape + QStringLiteral("showCrosshair"), style.showCrosshair).toBool();
    style.crosshairPercent = settings.value(shape + QStringLiteral("crosshairPercent"),
                                            style.crosshairPercent).toDouble();
    style.pickBoxPixels =
            settings.value(shape + QStringLiteral("pickBox"), style.pickBoxPixels).toDouble();

    // Grille.
    style.gridStyle = GridStyle(settings.value(shape + QStringLiteral("gridStyle"),
                                               int(style.gridStyle)).toInt());
    style.gridMajorEvery =
            settings.value(shape + QStringLiteral("gridMajorEvery"), style.gridMajorEvery).toInt();
    style.showGrid = settings.value(shape + QStringLiteral("showGrid"), style.showGrid).toBool();

    // Feuille et saisie.
    style.showSheetShadow =
            settings.value(shape + QStringLiteral("sheetShadow"), style.showSheetShadow).toBool();
    style.showDynamicInput = settings.value(shape + QStringLiteral("dynamicInput"),
                                            style.showDynamicInput).toBool();

    loadColor(settings, dark, "crosshair", style.crosshair);
    loadColor(settings, dark, "grid", style.grid);
    loadColor(settings, dark, "gridMajor", style.gridMajor);
    loadColor(settings, dark, "pageBackground", style.pageBackground);
    loadColor(settings, dark, "sheet", style.sheet);
}

void Appearance::save(const RenderStyle &style, bool dark)
{
    QSettings settings;
    const QString shape = QString::fromLatin1(kShape);

    settings.setValue(shape + QStringLiteral("showCrosshair"), style.showCrosshair);
    settings.setValue(shape + QStringLiteral("crosshairPercent"), style.crosshairPercent);
    settings.setValue(shape + QStringLiteral("pickBox"), style.pickBoxPixels);
    settings.setValue(shape + QStringLiteral("gridStyle"), int(style.gridStyle));
    settings.setValue(shape + QStringLiteral("gridMajorEvery"), style.gridMajorEvery);
    settings.setValue(shape + QStringLiteral("showGrid"), style.showGrid);
    settings.setValue(shape + QStringLiteral("sheetShadow"), style.showSheetShadow);
    settings.setValue(shape + QStringLiteral("dynamicInput"), style.showDynamicInput);

    // La couleur est ecrite en « #rrggbb » : lisible dans le fichier de
    // reglages, et reconstructible sans dependre de la serialisation de QColor.
    settings.setValue(colorKey(dark, "crosshair"), style.crosshair.name());
    settings.setValue(colorKey(dark, "grid"), style.grid.name());
    settings.setValue(colorKey(dark, "gridMajor"), style.gridMajor.name());
    settings.setValue(colorKey(dark, "pageBackground"), style.pageBackground.name());
    settings.setValue(colorKey(dark, "sheet"), style.sheet.name());
}

void Appearance::reset(bool dark)
{
    QSettings settings;
    settings.remove(QStringLiteral("display"));
    Q_UNUSED(dark); // les deux themes sont oublies ensemble : « valeurs d'origine » est entier
}

bool Appearance::hasOverrides(bool dark)
{
    const QSettings settings;
    if (settings.contains(QString::fromLatin1(kShape) + QStringLiteral("crosshairPercent")))
        return true;
    for (const char *name : kColorNames) {
        if (settings.contains(colorKey(dark, name)))
            return true;
    }
    return false;
}

} // namespace dsn
