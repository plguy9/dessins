// Lecture et ecriture d'archives ZIP.
//
// Qt expose bien QZipWriter, mais dans les en-tetes prives de QtGui : s'y
// appuyer, c'est accepter qu'une mise a jour mineure de Qt casse le format de
// fichier natif. Cent cinquante lignes et zlib suffisent, et le conteneur
// reste ouvrable par n'importe quel outil d'archivage.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>

namespace dsn {

class ZipWriter
{
public:
    explicit ZipWriter(QDateTime timestamp = QDateTime::currentDateTime());

    // Ajoute une entree. Les donnees sont compressees sauf si elles y perdent.
    void addFile(const QString &name, const QByteArray &data);

    // Archive complete, prete a etre ecrite sur disque.
    QByteArray archive() const;

    int entryCount() const { return int(m_entries.size()); }

private:
    struct Entry {
        QString name;
        quint32 crc = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint16 method = 0;
        quint32 localOffset = 0;
    };

    QByteArray m_body;
    QVector<Entry> m_entries;
    quint16 m_dosTime = 0;
    quint16 m_dosDate = 0;
};

class ZipReader
{
public:
    explicit ZipReader(const QByteArray &archive);

    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }

    QStringList entries() const;
    bool contains(const QString &name) const;
    QByteArray read(const QString &name) const;

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localOffset = 0;
    };

    QByteArray m_archive;
    QMap<QString, Entry> m_entries;
    QStringList m_order;
    bool m_valid = false;
    QString m_error;
};

} // namespace dsn
