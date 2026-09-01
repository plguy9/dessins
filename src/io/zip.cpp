#include "zip.h"

#include <zlib.h>

#include <cstring>

namespace dsn {

namespace {

constexpr quint32 kLocalSignature = 0x04034b50;
constexpr quint32 kCentralSignature = 0x02014b50;
constexpr quint32 kEndSignature = 0x06054b50;

void put16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
}

void put32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 24) & 0xff));
}

quint16 get16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return quint16(quint8(data.at(offset))) | (quint16(quint8(data.at(offset + 1))) << 8);
}

quint32 get32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return quint32(quint8(data.at(offset))) | (quint32(quint8(data.at(offset + 1))) << 8)
            | (quint32(quint8(data.at(offset + 2))) << 16)
            | (quint32(quint8(data.at(offset + 3))) << 24);
}

// Deflate brut, sans en-tete zlib : c'est ce qu'attend la methode 8 du ZIP.
QByteArray rawDeflate(const QByteArray &input)
{
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return QByteArray();

    QByteArray out;
    out.resize(int(deflateBound(&stream, uLong(input.size()))) + 32);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = uInt(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());

    const int status = deflate(&stream, Z_FINISH);
    const int written = int(stream.total_out);
    deflateEnd(&stream);
    if (status != Z_STREAM_END)
        return QByteArray();
    out.resize(written);
    return out;
}

QByteArray rawInflate(const QByteArray &input, quint32 expectedSize)
{
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return QByteArray();

    QByteArray out;
    out.resize(int(expectedSize));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = uInt(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());

    const int status = inflate(&stream, Z_FINISH);
    const int written = int(stream.total_out);
    inflateEnd(&stream);
    if (status != Z_STREAM_END && status != Z_OK)
        return QByteArray();
    out.resize(written);
    return out;
}

} // namespace

// --------------------------------------------------------------------------

ZipWriter::ZipWriter(QDateTime timestamp)
{
    const QDate date = timestamp.date();
    const QTime time = timestamp.time();
    // Format d'horodatage MS-DOS, hérité du format ZIP : annee depuis 1980,
    // secondes par pas de deux.
    m_dosDate = quint16(((date.year() - 1980) << 9) | (date.month() << 5) | date.day());
    m_dosTime = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
}

void ZipWriter::addFile(const QString &name, const QByteArray &data)
{
    Entry entry;
    entry.name = name;
    entry.uncompressedSize = quint32(data.size());
    entry.crc = quint32(::crc32(0, reinterpret_cast<const Bytef *>(data.constData()),
                                uInt(data.size())));
    entry.localOffset = quint32(m_body.size());

    QByteArray payload = rawDeflate(data);
    // Sur une petite entree deja compacte, deflate peut grossir : on stocke.
    if (payload.isEmpty() || payload.size() >= data.size()) {
        payload = data;
        entry.method = 0;
    } else {
        entry.method = 8;
    }
    entry.compressedSize = quint32(payload.size());

    const QByteArray nameBytes = name.toUtf8();
    put32(m_body, kLocalSignature);
    put16(m_body, 20);              // version minimale
    put16(m_body, 1 << 11);         // nom de fichier en UTF-8
    put16(m_body, entry.method);
    put16(m_body, m_dosTime);
    put16(m_body, m_dosDate);
    put32(m_body, entry.crc);
    put32(m_body, entry.compressedSize);
    put32(m_body, entry.uncompressedSize);
    put16(m_body, quint16(nameBytes.size()));
    put16(m_body, 0);               // pas de champ supplementaire
    m_body.append(nameBytes);
    m_body.append(payload);

    m_entries.append(entry);
}

QByteArray ZipWriter::archive() const
{
    QByteArray out = m_body;
    const quint32 centralOffset = quint32(out.size());

    for (const Entry &entry : m_entries) {
        const QByteArray nameBytes = entry.name.toUtf8();
        put32(out, kCentralSignature);
        put16(out, 20);            // version d'ecriture
        put16(out, 20);            // version minimale de lecture
        put16(out, 1 << 11);       // nom en UTF-8
        put16(out, entry.method);
        put16(out, m_dosTime);
        put16(out, m_dosDate);
        put32(out, entry.crc);
        put32(out, entry.compressedSize);
        put32(out, entry.uncompressedSize);
        put16(out, quint16(nameBytes.size()));
        put16(out, 0);             // champ supplementaire
        put16(out, 0);             // commentaire
        put16(out, 0);             // numero de disque
        put16(out, 0);             // attributs internes
        put32(out, 0);             // attributs externes
        put32(out, entry.localOffset);
        out.append(nameBytes);
    }

    const quint32 centralSize = quint32(out.size()) - centralOffset;
    put32(out, kEndSignature);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(m_entries.size()));
    put16(out, quint16(m_entries.size()));
    put32(out, centralSize);
    put32(out, centralOffset);
    put16(out, 0);
    return out;
}

// --------------------------------------------------------------------------

ZipReader::ZipReader(const QByteArray &archive) : m_archive(archive)
{
    // Le repertoire central se trouve par sa marque de fin, cherchee depuis la
    // queue du fichier : un commentaire d'archive peut suivre.
    int endOffset = -1;
    const int lowest = int(std::max(qsizetype(0), m_archive.size() - 66000));
    for (int i = m_archive.size() - 22; i >= lowest; --i) {
        if (get32(m_archive, i) == kEndSignature) {
            endOffset = i;
            break;
        }
    }
    if (endOffset < 0) {
        m_error = QStringLiteral("archive ZIP invalide : fin de repertoire introuvable");
        return;
    }

    const quint16 count = get16(m_archive, endOffset + 10);
    quint32 offset = get32(m_archive, endOffset + 16);

    for (quint16 i = 0; i < count; ++i) {
        if (get32(m_archive, int(offset)) != kCentralSignature) {
            m_error = QStringLiteral("archive ZIP invalide : entree %1 corrompue").arg(i);
            return;
        }
        Entry entry;
        entry.method = get16(m_archive, int(offset) + 10);
        entry.compressedSize = get32(m_archive, int(offset) + 20);
        entry.uncompressedSize = get32(m_archive, int(offset) + 24);
        const quint16 nameLength = get16(m_archive, int(offset) + 28);
        const quint16 extraLength = get16(m_archive, int(offset) + 30);
        const quint16 commentLength = get16(m_archive, int(offset) + 32);
        entry.localOffset = get32(m_archive, int(offset) + 42);

        const QString name =
                QString::fromUtf8(m_archive.mid(int(offset) + 46, nameLength));
        m_entries.insert(name, entry);
        m_order.append(name);

        offset += 46u + nameLength + extraLength + commentLength;
    }
    m_valid = true;
}

QStringList ZipReader::entries() const { return m_order; }

bool ZipReader::contains(const QString &name) const { return m_entries.contains(name); }

QByteArray ZipReader::read(const QString &name) const
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return QByteArray();

    const Entry &entry = it.value();
    const int local = int(entry.localOffset);
    if (get32(m_archive, local) != kLocalSignature)
        return QByteArray();

    // Les longueurs de nom et de champ supplementaire sont relues dans
    // l'en-tete local : elles peuvent differer de celles du repertoire central.
    const quint16 nameLength = get16(m_archive, local + 26);
    const quint16 extraLength = get16(m_archive, local + 28);
    const int dataOffset = local + 30 + nameLength + extraLength;
    const QByteArray payload = m_archive.mid(dataOffset, int(entry.compressedSize));

    if (entry.method == 0)
        return payload;
    if (entry.method == 8)
        return rawInflate(payload, entry.uncompressedSize);
    return QByteArray();
}

} // namespace dsn
