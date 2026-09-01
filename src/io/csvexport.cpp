#include "csvexport.h"

#include <QFile>
#include <QSaveFile>

namespace dsn {

QString CsvExport::escapeField(const QString &field, QChar separator)
{
    const bool needsQuotes = field.contains(separator) || field.contains(QLatin1Char('"'))
            || field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'));
    if (!needsQuotes)
        return field;
    QString escaped = field;
    escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QByteArray CsvExport::encode(const ReportTable &table, const CsvOptions &options)
{
    const QString eol = options.crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QString out;

    QStringList header;
    header.reserve(table.headers.size());
    for (const QString &cell : table.headers)
        header.append(escapeField(cell, options.separator));
    out += header.join(options.separator) + eol;

    for (const QStringList &row : table.rows) {
        QStringList cells;
        cells.reserve(row.size());
        for (QString cell : row) {
            if (options.decimalSeparator != QLatin1String(".")) {
                // Un nombre decimal a l'anglaise est interprete comme du texte
                // par un tableur configure en francais.
                bool numeric = false;
                cell.toDouble(&numeric);
                if (numeric && cell.contains(QLatin1Char('.')))
                    cell.replace(QLatin1Char('.'), options.decimalSeparator);
            }
            cells.append(escapeField(cell, options.separator));
        }
        out += cells.join(options.separator) + eol;
    }

    QByteArray bytes;
    if (options.byteOrderMark)
        bytes.append("\xEF\xBB\xBF", 3);
    bytes.append(out.toUtf8());
    return bytes;
}

bool CsvExport::write(const QString &path, const ReportTable &table, const CsvOptions &options,
                      QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray payload = encode(table, options);
    if (file.write(payload) != payload.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace dsn
