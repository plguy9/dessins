#include "profile.h"

namespace dsn {

QString WireNumberingRule::format(int value) const
{
    QString number = QString::number(value);
    if (padding > 0)
        number = number.rightJustified(padding, QLatin1Char('0'));
    return prefix + number;
}

QString WireNumberingRule::strategyTag(Strategy s)
{
    switch (s) {
    case Strategy::Sequential: return QStringLiteral("sequential");
    case Strategy::FolioColumn: return QStringLiteral("folio-column");
    case Strategy::PotentialName: return QStringLiteral("potential-name");
    }
    return QStringLiteral("folio-column");
}

WireNumberingRule::Strategy WireNumberingRule::strategyFromTag(const QString &tag)
{
    if (tag == QLatin1String("sequential")) return Strategy::Sequential;
    if (tag == QLatin1String("potential-name")) return Strategy::PotentialName;
    return Strategy::FolioColumn;
}

QString DesignationRule::format(const QString &prefix, int index) const
{
    QString number = QString::number(index);
    if (padding > 0)
        number = number.rightJustified(padding, QLatin1Char('0'));
    return (leadingDash ? QStringLiteral("-") : QString()) + prefix + number;
}

Profile Profile::iec()
{
    Profile p;
    p.id = QStringLiteral("iec");
    p.name = QStringLiteral("CEI 60617 / 81346");
    p.norm = QStringLiteral("IEC");
    p.unitSystem = QStringLiteral("metric");
    p.gridStep = 2.5;
    p.defaultSheetFormat = QStringLiteral("A3");
    p.wireNumbering.strategy = WireNumberingRule::Strategy::FolioColumn;
    p.wireNumbering.perFolio = true;
    p.designation.leadingDash = true;
    p.defaultConductors = { QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3"),
                            QStringLiteral("N"), QStringLiteral("PE") };
    return p;
}

Profile Profile::ansi()
{
    Profile p;
    p.id = QStringLiteral("ansi");
    p.name = QStringLiteral("ANSI / NFPA 79");
    p.norm = QStringLiteral("ANSI");
    p.unitSystem = QStringLiteral("imperial");
    // Un dixieme de pouce : le pas de grille usuel des schemas nord-americains.
    p.gridStep = 2.54;
    p.defaultSheetFormat = QStringLiteral("ANSI_B");
    p.wireNumbering.strategy = WireNumberingRule::Strategy::Sequential;
    p.wireNumbering.perFolio = false;
    p.wireNumbering.padding = 3;
    // Pas de tiret devant la designation dans l'usage nord-americain.
    p.designation.leadingDash = false;
    p.defaultConductors = { QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3"),
                            QStringLiteral("N"), QStringLiteral("GND") };
    return p;
}

Profile Profile::electronic()
{
    Profile p;
    p.id = QStringLiteral("electronic");
    p.name = QStringLiteral("Électronique");
    p.norm = QStringLiteral("IEC");
    p.unitSystem = QStringLiteral("metric");
    p.gridStep = 1.27; // un vingtieme de pouce, pas usuel des schemas de circuits
    p.defaultSheetFormat = QStringLiteral("A4");
    p.wireNumbering.strategy = WireNumberingRule::Strategy::PotentialName;
    p.wireNumbering.perFolio = false;
    p.wireNumbering.prefix = QStringLiteral("N");
    // Un schema electronique ne prefixe pas ses reperes : R1, C2, U3.
    p.designation.leadingDash = false;
    p.defaultConductors.clear();
    return p;
}

QList<Profile> Profile::all() { return { iec(), ansi(), electronic() }; }

Profile Profile::byId(const QString &id)
{
    const QList<Profile> profiles = all();
    for (const Profile &p : profiles) {
        if (p.id == id)
            return p;
    }
    return iec();
}

} // namespace dsn
