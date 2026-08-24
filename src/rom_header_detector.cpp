#include "rom_header_detector.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>
#include <QList>
#include <cstring>

namespace {

static quint16 le16(const char *p)
{
    const uchar *u = reinterpret_cast<const uchar *>(p);
    return static_cast<quint16>(u[0] | (static_cast<quint16>(u[1]) << 8));
}

static quint32 le32(const char *p)
{
    const uchar *u = reinterpret_cast<const uchar *>(p);
    return static_cast<quint32>(u[0]) |
           (static_cast<quint32>(u[1]) << 8) |
           (static_cast<quint32>(u[2]) << 16) |
           (static_cast<quint32>(u[3]) << 24);
}

static quint32 be32(const char *p)
{
    const uchar *u = reinterpret_cast<const uchar *>(p);
    return (static_cast<quint32>(u[0]) << 24) |
           (static_cast<quint32>(u[1]) << 16) |
           (static_cast<quint32>(u[2]) << 8) |
           static_cast<quint32>(u[3]);
}

static QByteArray readAt(QFile &file, qint64 offset, qint64 amount)
{
    if (offset < 0 || amount <= 0 || offset >= file.size())
        return QByteArray();
    if (!file.seek(offset))
        return QByteArray();
    return file.read(qMin(amount, file.size() - offset));
}

static QString cleanAscii(const QByteArray &bytes, bool stopAtNull = true)
{
    QByteArray out;
    out.reserve(bytes.size());
    bool previousSpace = false;
    for (int i = 0; i < bytes.size(); ++i) {
        const uchar ch = static_cast<uchar>(bytes.at(i));
        if (stopAtNull && ch == 0)
            break;
        if (ch >= 0x20 && ch <= 0x7e) {
            out.append(static_cast<char>(ch));
            previousSpace = false;
        } else if (!previousSpace) {
            out.append(' ');
            previousSpace = true;
        }
    }
    return QString::fromLatin1(out).simplified();
}

static bool mostlyPrintable(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return false;
    int printable = 0;
    int meaningful = 0;
    for (int i = 0; i < bytes.size(); ++i) {
        const uchar c = static_cast<uchar>(bytes.at(i));
        if (c == 0 || c == 0xff || c == 0x20)
            continue;
        ++meaningful;
        if (c >= 0x20 && c <= 0x7e)
            ++printable;
    }
    return meaningful > 0 && printable * 100 / meaningful >= 72;
}

static bool isClearlyNonRomFile(QFile &file)
{
    // Before trying permissive cartridge heuristics, reject well-known non-ROM
    // containers by their real binary signatures.  This is intentionally not
    // extension based: a WAV renamed to .bin still begins with RIFF/RF64+WAVE.
    const QByteArray h = readAt(file, 0, 4096);
    if (h.size() < 12)
        return false;

    const QByteArray four = h.left(4);
    if ((four == "RIFF" || four == "RF64") && h.mid(8, 4) == "WAVE")
        return true;
    if (four == "FORM" && (h.mid(8, 4) == "AIFF" || h.mid(8, 4) == "AIFC"))
        return true;
    if (four == "OggS" || four == "fLaC" || h.left(3) == "ID3")
        return true;

    if (h.left(8) == QByteArray("\x89PNG\r\n\x1a\n", 8) ||
        h.left(3) == QByteArray("\xff\xd8\xff", 3) ||
        h.left(6) == "GIF87a" || h.left(6) == "GIF89a" ||
        h.left(2) == "BM")
        return true;

    if (h.left(4) == "PK\x03\x04" || h.left(4) == "PK\x05\x06" ||
        h.left(6) == QByteArray("7z\xbc\xaf\x27\x1c", 6) ||
        h.left(7) == "Rar!\x1a\x07" ||
        h.left(2) == QByteArray("\x1f\x8b", 2) ||
        h.left(3) == "BZh" ||
        h.left(6) == QByteArray("\xfd" "7zXZ\x00", 6))
        return true;

    if (h.left(5) == "%PDF-" || h.left(4) == QByteArray("\x7f" "ELF", 4) ||
        h.left(16) == QByteArray("SQLite format 3\0", 16))
        return true;

    // PE/DOS programs are software executables, not console ROM images.
    if (h.left(2) == "MZ")
        return true;

    // MP4/MOV-family media and Matroska/WebM.
    if (h.size() >= 12 && h.mid(4, 4) == "ftyp")
        return true;
    if (h.left(4) == QByteArray("\x1a\x45\xdf\xa3", 4))
        return true;

    // Plain text/configuration files can occasionally satisfy weak printable
    // header heuristics.  Reject them when the first block is overwhelmingly
    // printable and contains ordinary line structure.
    int printable = 0;
    int controls = 0;
    for (int i = 0; i < h.size(); ++i) {
        const uchar c = static_cast<uchar>(h.at(i));
        if (c == '\r' || c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7e))
            ++printable;
        else if (c != 0)
            ++controls;
    }
    if (h.size() >= 256 && printable * 100 / h.size() >= 94 && controls < h.size() / 50 &&
        (h.contains('\n') || h.contains('\r')))
        return true;

    return false;
}

static RomHeaderInfo makeInfo(const QString &system, const QString &title,
                              const QString &format, int confidence,
                              const QString &internalId = QString())
{
    RomHeaderInfo info;
    info.isRom = true;
    info.system = system;
    info.title = title.simplified();
    info.format = format;
    info.confidence = confidence;
    info.internalId = internalId.simplified();
    return info;
}

static RomHeaderInfo detectNes(QFile &file)
{
    const QByteArray head = readAt(file, 0, 64 * 1024);
    if (head.size() >= 16 && head.mid(0, 4) == QByteArray("NES\x1a", 4))
        return makeInfo(QStringLiteral("Nintendo Entertainment System"), QString(),
                        QStringLiteral("iNES"), 100);

    if (head.size() >= 32 && head.mid(0, 4) == QByteArray("UNIF", 4)) {
        QString title;
        QString mapper;
        int pos = 32;
        while (pos + 8 <= head.size()) {
            const QByteArray id = head.mid(pos, 4);
            const quint32 length = le32(head.constData() + pos + 4);
            pos += 8;
            if (length > static_cast<quint32>(head.size() - pos))
                break;
            if (id == "NAME")
                title = cleanAscii(head.mid(pos, static_cast<int>(length)));
            else if (id == "MAPR")
                mapper = cleanAscii(head.mid(pos, static_cast<int>(length)));
            pos += static_cast<int>(length);
        }
        return makeInfo(QStringLiteral("Nintendo Entertainment System"), title,
                        QStringLiteral("UNIF"), 100, mapper);
    }
    return RomHeaderInfo();
}

static RomHeaderInfo detectGameBoy(QFile &file)
{
    static const uchar logo[48] = {
        0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,0x00,0x0C,0x00,0x0D,
        0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,
        0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E
    };
    const QByteArray head = readAt(file, 0, 0x160);
    if (head.size() < 0x150 || memcmp(head.constData() + 0x104, logo, sizeof(logo)) != 0)
        return RomHeaderInfo();

    const uchar cgb = static_cast<uchar>(head.at(0x143));
    const bool color = cgb == 0x80 || cgb == 0xC0;
    const int titleLength = color ? 15 : 16;
    const QString title = cleanAscii(head.mid(0x134, titleLength));
    const QString system = color ? QStringLiteral("Game Boy Color") : QStringLiteral("Game Boy");
    return makeInfo(system, title, QStringLiteral("Game Boy cartridge"), 100);
}

static RomHeaderInfo detectGba(QFile &file)
{
    const QByteArray head = readAt(file, 0, 0xC0);
    if (head.size() < 0xBE || static_cast<uchar>(head.at(0xB2)) != 0x96)
        return RomHeaderInfo();

    quint8 checksum = 0;
    for (int i = 0xA0; i <= 0xBC; ++i)
        checksum = static_cast<quint8>(checksum - static_cast<quint8>(head.at(i)));
    checksum = static_cast<quint8>(checksum - 0x19);
    if (checksum != static_cast<quint8>(head.at(0xBD)))
        return RomHeaderInfo();

    const QString title = cleanAscii(head.mid(0xA0, 12));
    const QString code = cleanAscii(head.mid(0xAC, 4));
    return makeInfo(QStringLiteral("Game Boy Advance"), title,
                    QStringLiteral("GBA cartridge"), 100, code);
}

static RomHeaderInfo detectNds(QFile &file)
{
    const QByteArray head = readAt(file, 0, 0x200);
    if (head.size() < 0x160)
        return RomHeaderInfo();

    const QByteArray gameCode = head.mid(0x0C, 4);
    bool codeOk = true;
    for (int i = 0; i < gameCode.size(); ++i) {
        const uchar c = static_cast<uchar>(gameCode.at(i));
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) {
            codeOk = false;
            break;
        }
    }
    if (!codeOk)
        return RomHeaderInfo();

    // Official NDS headers use this fixed Nintendo-logo CRC.
    if (static_cast<uchar>(head.at(0x15C)) != 0x56 ||
        static_cast<uchar>(head.at(0x15D)) != 0xCF)
        return RomHeaderInfo();

    const QString title = cleanAscii(head.mid(0x00, 12));
    return makeInfo(QStringLiteral("Nintendo DS"), title,
                    QStringLiteral("Nintendo DS cartridge"), 100,
                    QString::fromLatin1(gameCode));
}

static RomHeaderInfo detectN64(QFile &file)
{
    QByteArray head = readAt(file, 0, 0x1000);
    if (head.size() < 0x40)
        return RomHeaderInfo();

    const quint32 magic = be32(head.constData());
    if (magic == 0x37804012u) {
        for (int i = 0; i + 1 < head.size(); i += 2) {
            const char a = head.at(i);
            head[i] = head.at(i + 1);
            head[i + 1] = a;
        }
    } else if (magic == 0x40123780u) {
        for (int i = 0; i + 3 < head.size(); i += 4) {
            const char a = head.at(i);
            const char b = head.at(i + 1);
            head[i] = head.at(i + 3);
            head[i + 1] = head.at(i + 2);
            head[i + 2] = b;
            head[i + 3] = a;
        }
    } else if (magic != 0x80371240u) {
        return RomHeaderInfo();
    }

    const QString title = cleanAscii(head.mid(0x20, 20));
    const QString id = cleanAscii(head.mid(0x3B, 4));
    return makeInfo(QStringLiteral("Nintendo 64"), title,
                    QStringLiteral("Nintendo 64 cartridge"), 100, id);
}

static RomHeaderInfo detectSnesAt(QFile &file, qint64 offset)
{
    const QByteArray h = readAt(file, offset, 0x40);
    if (h.size() < 0x40)
        return RomHeaderInfo();

    const QByteArray rawTitle = h.mid(0, 21);
    if (!mostlyPrintable(rawTitle))
        return RomHeaderInfo();

    const quint8 mapMode = static_cast<quint8>(h.at(0x15));
    const quint16 complement = le16(h.constData() + 0x1C);
    const quint16 checksum = le16(h.constData() + 0x1E);
    const quint16 reset = le16(h.constData() + 0x3C);

    int score = 0;
    if ((checksum ^ complement) == 0xFFFFu && checksum != 0 && checksum != 0xFFFFu)
        score += 4;
    const int mapLow = mapMode & 0x3F;
    if (mapLow == 0x20 || mapLow == 0x21 || mapLow == 0x22 || mapLow == 0x23 ||
        mapLow == 0x25 || mapLow == 0x30 || mapLow == 0x31 || mapLow == 0x32 || mapLow == 0x35)
        score += 2;
    if (reset >= 0x8000u)
        score += 2;
    if (mostlyPrintable(rawTitle))
        score += 2;

    if (score < 7)
        return RomHeaderInfo();
    return makeInfo(QStringLiteral("Super Nintendo"), cleanAscii(rawTitle, false),
                    QStringLiteral("SNES cartridge"), qMin(100, score * 10));
}

static RomHeaderInfo detectSnes(QFile &file)
{
    const qint64 size = file.size();
    // Real SNES cartridge dumps are normally bank-aligned, optionally with a
    // 512-byte copier header.  Requiring that structure removes accidental
    // matches inside arbitrary audio/data files while retaining standard dumps.
    if (size < 32 * 1024 ||
        ((size % 0x8000) != 0 && (size < 512 || ((size - 512) % 0x8000) != 0)))
        return RomHeaderInfo();

    const qint64 offsets[] = {
        0x7FC0, 0xFFC0, 0x40FFC0,
        0x7FC0 + 512, 0xFFC0 + 512, 0x40FFC0 + 512
    };
    RomHeaderInfo best;
    for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        if (offsets[i] + 0x40 > size)
            continue;
        const RomHeaderInfo candidate = detectSnesAt(file, offsets[i]);
        if (candidate.isRom && candidate.confidence > best.confidence)
            best = candidate;
    }
    return best;
}

static RomHeaderInfo detectGenesis(QFile &file)
{
    const QByteArray head = readAt(file, 0, 0x220);
    if (head.size() < 0x200 || head.mid(0x100, 4) != "SEGA")
        return RomHeaderInfo();

    const QString domestic = cleanAscii(head.mid(0x120, 48), false);
    const QString overseas = cleanAscii(head.mid(0x150, 48), false);
    QString title = overseas;
    if (title.isEmpty() || title == QStringLiteral("JUE"))
        title = domestic;
    const QString serial = cleanAscii(head.mid(0x180, 14), false);
    return makeInfo(QStringLiteral("Sega Genesis / Mega Drive"), title,
                    QStringLiteral("Mega Drive / Genesis cartridge"), 100, serial);
}

static RomHeaderInfo detectSmsGameGear(QFile &file)
{
    const qint64 offsets[] = { 0x7FF0, 0x3FF0, 0x1FF0 };
    for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        const QByteArray h = readAt(file, offsets[i], 16);
        if (h.size() != 16 || h.mid(0, 8) != "TMR SEGA")
            continue;
        const quint8 region = static_cast<quint8>(h.at(15)) >> 4;
        const bool gameGear = region >= 5 && region <= 7;
        const QString system = gameGear ? QStringLiteral("Sega Game Gear")
                                        : QStringLiteral("Sega Master System");
        const QString product = QStringLiteral("%1-%2")
            .arg(static_cast<quint8>(h.at(12)), 2, 16, QLatin1Char('0'))
            .arg(static_cast<quint8>(h.at(13)), 2, 16, QLatin1Char('0'));
        return makeInfo(system, QString(), QStringLiteral("Sega 8-bit cartridge"), 100, product);
    }
    return RomHeaderInfo();
}

static RomHeaderInfo detectSaturnDreamcast(QFile &file)
{
    const QByteArray first = readAt(file, 0, 128 * 1024);
    int pos = first.indexOf("SEGA SEGASATURN ");
    if (pos >= 0 && pos + 0xD0 <= first.size()) {
        const QString title = cleanAscii(first.mid(pos + 0x60, 0x70), false);
        const QString product = cleanAscii(first.mid(pos + 0x20, 10), false);
        return makeInfo(QStringLiteral("Sega Saturn"), title,
                        QStringLiteral("Sega Saturn boot header"), 100, product);
    }

    pos = first.indexOf("SEGA SEGAKATANA ");
    if (pos >= 0 && pos + 0x100 <= first.size()) {
        const QString title = cleanAscii(first.mid(pos + 0x80, 0x80), false);
        const QString product = cleanAscii(first.mid(pos + 0x40, 10), false);
        return makeInfo(QStringLiteral("Sega Dreamcast"), title,
                        QStringLiteral("Dreamcast IP.BIN"), 100, product);
    }
    return RomHeaderInfo();
}

static RomHeaderInfo detectGameCubeWii(QFile &file)
{
    const QByteArray h = readAt(file, 0, 0x100);
    if (h.size() < 0x80)
        return RomHeaderInfo();

    const quint32 wiiMagic = be32(h.constData() + 0x18);
    const quint32 gcMagic = be32(h.constData() + 0x1C);
    QString system;
    if (wiiMagic == 0x5D1C9EA3u)
        system = QStringLiteral("Nintendo Wii");
    else if (gcMagic == 0xC2339F3Du)
        system = QStringLiteral("Nintendo GameCube");
    else
        return RomHeaderInfo();

    const QString title = cleanAscii(h.mid(0x20, 0x60));
    const QString gameId = cleanAscii(h.mid(0x00, 6));
    return makeInfo(system, title, QStringLiteral("Nintendo optical disc"), 100, gameId);
}

static RomHeaderInfo detectAtariLynx(QFile &file)
{
    const QByteArray h = readAt(file, 0, 64);
    if (h.size() >= 60 && h.mid(0, 4) == "LYNX") {
        const QString title = cleanAscii(h.mid(10, 32), false);
        const QString manufacturer = cleanAscii(h.mid(42, 16), false);
        return makeInfo(QStringLiteral("Atari Lynx"), title,
                        QStringLiteral("LNX cartridge"), 100, manufacturer);
    }

    if (h.size() >= 64 && h.mid(1, 9) == "ATARI7800") {
        const QString title = cleanAscii(h.mid(17, 32), false);
        return makeInfo(QStringLiteral("Atari 7800"), title,
                        QStringLiteral("A78 cartridge"), 100);
    }
    return RomHeaderInfo();
}

static RomHeaderInfo detectC64Crt(QFile &file)
{
    const QByteArray h = readAt(file, 0, 0x60);
    if (h.size() >= 0x40 && h.mid(0, 16) == "C64 CARTRIDGE   ") {
        const QString title = cleanAscii(h.mid(0x20, 32), false);
        return makeInfo(QStringLiteral("Commodore 64"), title,
                        QStringLiteral("C64 CRT cartridge"), 100);
    }
    return RomHeaderInfo();
}

static RomHeaderInfo detect3dsSwitch(QFile &file)
{
    const QByteArray h = readAt(file, 0, 0x200);
    if (h.size() >= 0x180 && h.mid(0x100, 4) == "NCCH") {
        const QString product = cleanAscii(h.mid(0x150, 16), false);
        return makeInfo(QStringLiteral("Nintendo 3DS"), product,
                        QStringLiteral("Nintendo 3DS NCCH"), 100, product);
    }
    if (h.size() >= 0x110 && h.mid(0x100, 4) == "NCSD")
        return makeInfo(QStringLiteral("Nintendo 3DS"), QString(),
                        QStringLiteral("Nintendo 3DS NCSD"), 100);
    if (h.size() >= 4 && h.mid(0, 4) == "PFS0")
        return makeInfo(QStringLiteral("Nintendo Switch"), QString(),
                        QStringLiteral("Nintendo Switch NSP/PFS0"), 100);
    if (h.size() >= 0x104 && h.mid(0x100, 4) == "HEAD")
        return makeInfo(QStringLiteral("Nintendo Switch"), QString(),
                        QStringLiteral("Nintendo Switch XCI"), 95);
    return RomHeaderInfo();
}

static QString parseSfoTitle(const QByteArray &sfo)
{
    if (sfo.size() < 20 || sfo.mid(0, 4) != QByteArray("\0PSF", 4))
        return QString();
    const quint32 keyTable = le32(sfo.constData() + 8);
    const quint32 dataTable = le32(sfo.constData() + 12);
    const quint32 count = le32(sfo.constData() + 16);
    if (count > 1024)
        return QString();

    for (quint32 i = 0; i < count; ++i) {
        const quint32 pos = 20 + i * 16;
        if (pos + 16 > static_cast<quint32>(sfo.size()))
            break;
        const quint16 keyOffset = le16(sfo.constData() + pos);
        const quint32 dataLen = le32(sfo.constData() + pos + 4);
        const quint32 dataOffset = le32(sfo.constData() + pos + 12);
        const quint32 keyPos = keyTable + keyOffset;
        if (keyPos >= static_cast<quint32>(sfo.size()))
            continue;
        const int zero = sfo.indexOf('\0', static_cast<int>(keyPos));
        if (zero < 0)
            continue;
        const QByteArray key = sfo.mid(static_cast<int>(keyPos), zero - static_cast<int>(keyPos));
        if (key != "TITLE")
            continue;
        const quint32 valuePos = dataTable + dataOffset;
        if (valuePos >= static_cast<quint32>(sfo.size()))
            return QString();
        return cleanAscii(sfo.mid(static_cast<int>(valuePos), static_cast<int>(qMin<quint32>(dataLen, 512u))));
    }
    return QString();
}

static RomHeaderInfo detectPbp(QFile &file)
{
    const QByteArray h = readAt(file, 0, 40);
    if (h.size() < 40 || h.mid(0, 4) != QByteArray("\0PBP", 4))
        return RomHeaderInfo();
    const quint32 paramOffset = le32(h.constData() + 8);
    const quint32 icon0Offset = le32(h.constData() + 12);
    if (icon0Offset <= paramOffset || icon0Offset - paramOffset > 1024 * 1024)
        return makeInfo(QStringLiteral("PlayStation Portable"), QString(),
                        QStringLiteral("PBP"), 100);
    const QByteArray sfo = readAt(file, paramOffset, icon0Offset - paramOffset);
    return makeInfo(QStringLiteral("PlayStation Portable"), parseSfoTitle(sfo),
                    QStringLiteral("PBP"), 100);
}

struct DiscLayout
{
    int sectorSize;
    int userOffset;
};

static QByteArray readDiscUser(QFile &file, const DiscLayout &layout, quint32 lba, quint32 bytes)
{
    QByteArray out;
    out.reserve(static_cast<int>(qMin<quint32>(bytes, 4 * 1024 * 1024u)));
    quint32 remaining = bytes;
    quint32 sector = lba;
    while (remaining > 0 && out.size() < 4 * 1024 * 1024) {
        const qint64 absolute = static_cast<qint64>(sector) * layout.sectorSize + layout.userOffset;
        const QByteArray block = readAt(file, absolute, qMin<quint32>(remaining, 2048u));
        if (block.isEmpty())
            break;
        out += block;
        if (block.size() < 2048)
            break;
        remaining -= qMin<quint32>(remaining, 2048u);
        ++sector;
    }
    return out;
}

struct IsoEntry
{
    QString name;
    quint32 extent;
    quint32 size;
    bool directory;
};

static QList<IsoEntry> parseIsoDirectory(const QByteArray &dir)
{
    QList<IsoEntry> entries;
    int pos = 0;
    while (pos < dir.size()) {
        const int len = static_cast<uchar>(dir.at(pos));
        if (len == 0) {
            pos = ((pos / 2048) + 1) * 2048;
            continue;
        }
        if (pos + len > dir.size() || len < 34)
            break;
        const char *r = dir.constData() + pos;
        const quint32 extent = le32(r + 2);
        const quint32 size = le32(r + 10);
        const bool directory = (static_cast<uchar>(r[25]) & 0x02) != 0;
        const int nameLen = static_cast<uchar>(r[32]);
        if (33 + nameLen <= len && nameLen > 0) {
            QByteArray rawName(r + 33, nameLen);
            if (!(nameLen == 1 && (rawName.at(0) == 0 || rawName.at(0) == 1))) {
                int semi = rawName.indexOf(';');
                if (semi >= 0)
                    rawName.truncate(semi);
                IsoEntry e;
                e.name = QString::fromLatin1(rawName).toUpper();
                e.extent = extent;
                e.size = size;
                e.directory = directory;
                entries.append(e);
            }
        }
        pos += len;
    }
    return entries;
}

static bool findEntry(const QList<IsoEntry> &entries, const QString &name, IsoEntry *out)
{
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            if (out)
                *out = entries.at(i);
            return true;
        }
    }
    return false;
}

static RomHeaderInfo detectPlayStationIso(QFile &file)
{
    const DiscLayout layouts[] = { {2048, 0}, {2352, 16}, {2352, 24}, {2336, 8} };
    for (unsigned int li = 0; li < sizeof(layouts) / sizeof(layouts[0]); ++li) {
        const DiscLayout layout = layouts[li];
        const QByteArray pvd = readDiscUser(file, layout, 16, 2048);
        if (pvd.size() < 2048 || static_cast<uchar>(pvd.at(0)) != 1 || pvd.mid(1, 5) != "CD001")
            continue;

        const QString volume = cleanAscii(pvd.mid(40, 32), false);
        const char *root = pvd.constData() + 156;
        const int rootLen = static_cast<uchar>(root[0]);
        if (rootLen < 34)
            continue;
        const quint32 rootExtent = le32(root + 2);
        const quint32 rootSize = le32(root + 10);
        const QByteArray rootData = readDiscUser(file, layout, rootExtent, qMin(rootSize, 1024u * 1024u));
        const QList<IsoEntry> rootEntries = parseIsoDirectory(rootData);

        IsoEntry pspGame;
        if (findEntry(rootEntries, QStringLiteral("PSP_GAME"), &pspGame) && pspGame.directory) {
            const QByteArray pspDirData = readDiscUser(file, layout, pspGame.extent, qMin(pspGame.size, 512u * 1024u));
            const QList<IsoEntry> pspEntries = parseIsoDirectory(pspDirData);
            IsoEntry param;
            QString title;
            if (findEntry(pspEntries, QStringLiteral("PARAM.SFO"), &param) && !param.directory) {
                const QByteArray sfo = readDiscUser(file, layout, param.extent, qMin(param.size, 1024u * 1024u));
                title = parseSfoTitle(sfo);
            }
            return makeInfo(QStringLiteral("PlayStation Portable"), title.isEmpty() ? volume : title,
                            QStringLiteral("PSP UMD ISO"), 100);
        }

        IsoEntry systemCnf;
        if (findEntry(rootEntries, QStringLiteral("SYSTEM.CNF"), &systemCnf) && !systemCnf.directory) {
            const QByteArray cnf = readDiscUser(file, layout, systemCnf.extent, qMin(systemCnf.size, 64u * 1024u));
            const QByteArray upper = cnf.toUpper();
            if (upper.contains("BOOT2"))
                return makeInfo(QStringLiteral("PlayStation 2"), volume,
                                QStringLiteral("PlayStation 2 optical disc"), 98,
                                cleanAscii(cnf.left(512), false));
            if (upper.contains("BOOT"))
                return makeInfo(QStringLiteral("PlayStation"), volume,
                                QStringLiteral("PlayStation optical disc"), 96,
                                cleanAscii(cnf.left(512), false));
        }
    }
    return RomHeaderInfo();
}

static RomHeaderInfo detectXboxDisc(QFile &file)
{
    const QByteArray sig = readAt(file, 32 * 2048, 64);
    if (sig.contains("MICROSOFT*XBOX*MEDIA"))
        return makeInfo(QStringLiteral("Xbox"), QString(),
                        QStringLiteral("Xbox XDVDFS disc"), 98);
    return RomHeaderInfo();
}

} // namespace

namespace RomHeaderDetector {

RomHeaderInfo detect(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || fi.size() < 64)
        return RomHeaderInfo();

    // Reading every file is deliberate: recognition is based on internal
    // signatures and metadata, never on the filename or extension.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return RomHeaderInfo();

    if (isClearlyNonRomFile(file))
        return RomHeaderInfo();

    const qint64 size = file.size();
    if (size > (static_cast<qint64>(32) * 1024 * 1024 * 1024))
        return RomHeaderInfo();

    RomHeaderInfo best;
    const qint64 mb = 1024 * 1024;

    // Size gates are deliberately broad and are not extension-based.  They
    // avoid dozens of random seeks for obvious multi-gigabyte non-cartridge
    // files while still covering oversized dumps and padded images.
    if (size <= 64 * mb) {
        const RomHeaderInfo candidates[] = {
            detectNes(file), detectGameBoy(file), detectGba(file), detectN64(file),
            detectSnes(file), detectGenesis(file), detectSmsGameGear(file),
            detectAtariLynx(file), detectC64Crt(file)
        };
        for (unsigned int i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
            if (candidates[i].isRom && candidates[i].confidence > best.confidence)
                best = candidates[i];
        if (best.confidence >= 100)
            return best;
    }

    if (size <= static_cast<qint64>(2) * 1024 * mb) {
        const RomHeaderInfo nds = detectNds(file);
        if (nds.isRom && nds.confidence > best.confidence) best = nds;
    }

    const RomHeaderInfo modernNintendo = detect3dsSwitch(file);
    if (modernNintendo.isRom && modernNintendo.confidence > best.confidence) best = modernNintendo;
    if (best.confidence >= 100) return best;

    const RomHeaderInfo pbp = detectPbp(file);
    if (pbp.isRom && pbp.confidence > best.confidence) best = pbp;
    if (best.confidence >= 100) return best;

    const RomHeaderInfo segaDisc = detectSaturnDreamcast(file);
    if (segaDisc.isRom && segaDisc.confidence > best.confidence) best = segaDisc;
    if (best.confidence >= 100) return best;

    if (size >= 4 * mb) {
        const RomHeaderInfo nintendoDisc = detectGameCubeWii(file);
        if (nintendoDisc.isRom && nintendoDisc.confidence > best.confidence) best = nintendoDisc;
        if (best.confidence >= 100) return best;

        const RomHeaderInfo playStationDisc = detectPlayStationIso(file);
        if (playStationDisc.isRom && playStationDisc.confidence > best.confidence) best = playStationDisc;
    }

    if (size >= 100 * mb) {
        const RomHeaderInfo xbox = detectXboxDisc(file);
        if (xbox.isRom && xbox.confidence > best.confidence) best = xbox;
    }

    return best;
}

} // namespace RomHeaderDetector
