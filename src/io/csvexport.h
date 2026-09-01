// Export des rapports en CSV.
//
// Le CSV est le format qu'un acheteur ou un cableur ouvrira dans un tableur.
// Deux details decident de son utilisabilite reelle : le separateur attendu par
// la version locale du tableur, et la marque d'octets qui evite les accents
// casses a l'ouverture sous Windows.
#pragma once

#include "rules/reports.h"

#include <QString>

namespace dsn {

struct CsvOptions {
    QChar separator = QLatin1Char(';'); // le tableur francophone attend le point-virgule
    bool byteOrderMark = true;          // sans quoi Excel casse les accents
    bool crlf = true;
    QString decimalSeparator = QStringLiteral(",");
};

class CsvExport
{
public:
    static QByteArray encode(const ReportTable &table, const CsvOptions &options = {});
    static bool write(const QString &path, const ReportTable &table,
                      const CsvOptions &options = {}, QString *error = nullptr);

    // Echappe un champ selon RFC 4180 : guillemets doubles si le champ contient
    // le separateur, un guillemet ou un saut de ligne.
    static QString escapeField(const QString &field, QChar separator);
};

} // namespace dsn
