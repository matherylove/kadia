#include "screenscraper_client.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QIODevice>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wininet.h>
#endif

namespace {

struct ApiConfig
{
    QString devId;
    QString devPassword;
    QString softwareName;
    QString user;
    QString password;
    QString region;
};

static QString appDataBase()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return base;
}

static QString settingsPath()
{
    return QDir(appDataBase()).filePath(QStringLiteral("screenscraper.ini"));
}

static QString coversPath()
{
    const QString p = QDir(appDataBase()).filePath(QStringLiteral("covers"));
    QDir().mkpath(p);
    return p;
}

static QString configValue(QSettings &s, const QString &key, const char *envName,
                           const QString &fallback = QString())
{
    const QByteArray env = qgetenv(envName);
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env).trimmed();
    return s.value(key, fallback).toString().trimmed();
}

static ApiConfig readConfig()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("ScreenScraper"));
    ApiConfig c;
    c.devId = configValue(s, QStringLiteral("devid"), "KADIA_SS_DEV_ID");
    c.devPassword = configValue(s, QStringLiteral("devpassword"), "KADIA_SS_DEV_PASSWORD");
    c.softwareName = configValue(s, QStringLiteral("softname"), "KADIA_SS_SOFTNAME",
                                 QStringLiteral("MatheryKadia"));
    c.user = configValue(s, QStringLiteral("ssid"), "KADIA_SS_USER");
    c.password = configValue(s, QStringLiteral("sspassword"), "KADIA_SS_PASSWORD");
    c.region = configValue(s, QStringLiteral("region"), "KADIA_SS_REGION",
                           QStringLiteral("us"));
    s.endGroup();
    return c;
}

static bool hasApiConfig(const ApiConfig &c)
{
    return !c.devId.isEmpty() && !c.devPassword.isEmpty() &&
           !c.user.isEmpty() && !c.password.isEmpty();
}

#ifdef Q_OS_WIN
static bool httpGet(const QUrl &url, QByteArray *data)
{
    if (!data)
        return false;
    data->clear();

    HINTERNET internet = InternetOpenW(L"MatheryKadia/1.0", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
    if (!internet)
        return false;

    const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                        INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_UI |
                        INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_SECURE |
                        INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
                        INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;

    const QString urlString = url.toString();
    HINTERNET request = InternetOpenUrlW(internet,
                                         reinterpret_cast<LPCWSTR>(urlString.utf16()),
                                         0, 0, flags, 0);
    if (!request) {
        InternetCloseHandle(internet);
        return false;
    }

    char buffer[8192];
    DWORD bytesRead = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        data->append(buffer, int(bytesRead));
        bytesRead = 0;
    }

    InternetCloseHandle(request);
    InternetCloseHandle(internet);
    return !data->isEmpty();
}
#else
static bool httpGet(const QUrl &, QByteArray *) { return false; }
#endif

static QString systemIdForName(const QString &system)
{
    const QString s = system.trimmed().toLower();
    if (s == QStringLiteral("nintendo entertainment system")) return QStringLiteral("3");
    if (s == QStringLiteral("super nintendo")) return QStringLiteral("4");
    if (s == QStringLiteral("nintendo 64")) return QStringLiteral("14");
    if (s == QStringLiteral("game boy")) return QStringLiteral("9");
    if (s == QStringLiteral("game boy color")) return QStringLiteral("10");
    if (s == QStringLiteral("game boy advance")) return QStringLiteral("12");
    if (s == QStringLiteral("nintendo ds")) return QStringLiteral("15");
    if (s == QStringLiteral("nintendo 3ds")) return QStringLiteral("17");
    if (s == QStringLiteral("sega master system")) return QStringLiteral("2");
    if (s == QStringLiteral("sega genesis / mega drive")) return QStringLiteral("1");
    if (s == QStringLiteral("sega game gear")) return QStringLiteral("21");
    if (s == QStringLiteral("sega saturn")) return QStringLiteral("22");
    if (s == QStringLiteral("sega dreamcast")) return QStringLiteral("23");
    if (s == QStringLiteral("playstation")) return QStringLiteral("57");
    if (s == QStringLiteral("playstation 2")) return QStringLiteral("58");
    if (s == QStringLiteral("playstation portable")) return QStringLiteral("61");
    if (s == QStringLiteral("atari 2600")) return QStringLiteral("26");
    if (s == QStringLiteral("atari lynx")) return QStringLiteral("28");
    if (s == QStringLiteral("pc engine / turbografx-16")) return QStringLiteral("31");
    if (s == QStringLiteral("arcade")) return QStringLiteral("75");
    return QString();
}

static QString cleanedTitleCandidate(const QString &v)
{
    QString out = v;
    out.replace(QChar('_'), QChar(' '));
    out = out.simplified();
    return out;
}

static QString firstUsefulTitle(const QStringList &candidates)
{
    for (int i = 0; i < candidates.size(); ++i) {
        const QString c = cleanedTitleCandidate(candidates.at(i));
        if (!c.isEmpty() && c.length() >= 2)
            return c;
    }
    return QString();
}


struct FileHashes
{
    QString crc32;
    QString md5;
    QString sha1;
};

static quint32 crc32Update(quint32 crc, const unsigned char *data, int len)
{
    static quint32 table[256];
    static bool tableReady = false;
    if (!tableReady) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        tableReady = true;
    }

    quint32 c = crc;
    for (int i = 0; i < len; ++i)
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    return c;
}

static FileHashes computeFileHashes(const QString &path)
{
    FileHashes out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;

    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    quint32 crc = 0xFFFFFFFFU;

    while (!f.atEnd()) {
        const QByteArray chunk = f.read(1024 * 256);
        if (chunk.isEmpty())
            break;
        md5.addData(chunk);
        sha1.addData(chunk);
        crc = crc32Update(crc, reinterpret_cast<const unsigned char *>(chunk.constData()), chunk.size());
    }

    crc ^= 0xFFFFFFFFU;
    out.crc32 = QString::number(crc, 16).toUpper().rightJustified(8, QLatin1Char('0'));
    out.md5 = QString::fromLatin1(md5.result().toHex()).toLower();
    out.sha1 = QString::fromLatin1(sha1.result().toHex()).toLower();
    return out;
}

static QString normalizeRomNameForQuery(const QString &raw, const QString &fallbackFileName)
{
    QString romName = raw.trimmed();
    if (romName.isEmpty())
        romName = fallbackFileName;
    if (romName.isEmpty())
        romName = QStringLiteral("unknown");
    if (!romName.contains(QChar('.')))
        romName += QStringLiteral(".zip");
    return romName;
}

static QString fileExtFromUrl(const QString &url)
{
    const QString path = QUrl(url).path();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg") ||
        suffix == QStringLiteral("png") || suffix == QStringLiteral("bmp") ||
        suffix == QStringLiteral("webp"))
        return suffix;
    return QStringLiteral("png");
}

static QString hashedFileName(const QString &path, const QString &ext)
{
    const QByteArray hash = QCryptographicHash::hash(QDir::cleanPath(path).toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(hash) + QStringLiteral(".") + ext;
}

static QString extractUrlFromObject(const QJsonObject &obj)
{
    const QStringList keys = obj.keys();
    for (int i = 0; i < keys.size(); ++i) {
        const QString key = keys.at(i);
        const QJsonValue v = obj.value(key);
        if (v.isString()) {
            const QString s = v.toString().trimmed();
            if ((key.contains(QStringLiteral("url"), Qt::CaseInsensitive) ||
                 key.contains(QStringLiteral("media"), Qt::CaseInsensitive) ||
                 key.contains(QStringLiteral("path"), Qt::CaseInsensitive)) &&
                (s.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                 s.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)))
                return s;
        }
    }
    return QString();
}

static QString findCoverUrlRecursive(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString();
        const QString media = obj.value(QStringLiteral("media")).toString();
        const QString kind = type + QLatin1Char(' ') + media;
        if (kind.contains(QStringLiteral("box-2d"), Qt::CaseInsensitive) ||
            kind.contains(QStringLiteral("box2d"), Qt::CaseInsensitive) ||
            kind.contains(QStringLiteral("box-3d"), Qt::CaseInsensitive) ||
            kind.contains(QStringLiteral("box3d"), Qt::CaseInsensitive) ||
            kind.contains(QStringLiteral("support-2d"), Qt::CaseInsensitive) ||
            kind.contains(QStringLiteral("wheel"), Qt::CaseInsensitive)) {
            const QString url = extractUrlFromObject(obj);
            if (!url.isEmpty())
                return url;
        }

        const QStringList keys = obj.keys();
        for (int i = 0; i < keys.size(); ++i) {
            const QString key = keys.at(i);
            const QJsonValue child = obj.value(key);
            if (child.isString()) {
                const QString s = child.toString().trimmed();
                if ((s.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                     s.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) &&
                    (key.contains(QStringLiteral("box"), Qt::CaseInsensitive) ||
                     key.contains(QStringLiteral("cover"), Qt::CaseInsensitive) ||
                     key.contains(QStringLiteral("support"), Qt::CaseInsensitive)))
                    return s;
            }
        }
        for (int i = 0; i < keys.size(); ++i) {
            const QString url = findCoverUrlRecursive(obj.value(keys.at(i)));
            if (!url.isEmpty())
                return url;
        }
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QString url = findCoverUrlRecursive(array.at(i));
            if (!url.isEmpty())
                return url;
        }
    }
    return QString();
}

static QString findLocalizedStringRecursive(const QJsonValue &value,
                                            const QStringList &preferredKeyParts)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QStringList keys = obj.keys();
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < keys.size(); ++i) {
                const QString key = keys.at(i);
                const QString lower = key.toLower();
                bool keyMatches = false;
                for (int j = 0; j < preferredKeyParts.size(); ++j) {
                    if (lower.contains(preferredKeyParts.at(j).toLower())) {
                        keyMatches = true;
                        break;
                    }
                }
                if (!keyMatches)
                    continue;
                const QJsonValue child = obj.value(key);
                if (child.isString()) {
                    const QString text = child.toString().trimmed();
                    if (!text.isEmpty() && !text.startsWith(QStringLiteral("http"), Qt::CaseInsensitive))
                        return text;
                }
                if (pass == 1) {
                    const QString nested = findLocalizedStringRecursive(child, preferredKeyParts);
                    if (!nested.isEmpty())
                        return nested;
                }
            }
        }
        for (int i = 0; i < keys.size(); ++i) {
            const QString nested = findLocalizedStringRecursive(obj.value(keys.at(i)), preferredKeyParts);
            if (!nested.isEmpty())
                return nested;
        }
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QString nested = findLocalizedStringRecursive(array.at(i), preferredKeyParts);
            if (!nested.isEmpty())
                return nested;
        }
    }
    return QString();
}

static QJsonObject responseGameObject(const QJsonDocument &doc)
{
    if (!doc.isObject())
        return QJsonObject();
    const QJsonObject root = doc.object();
    const QJsonObject response = root.value(QStringLiteral("response")).toObject();
    QJsonValue jeu = response.value(QStringLiteral("jeu"));
    if (jeu.isArray() && !jeu.toArray().isEmpty())
        jeu = jeu.toArray().first();
    return jeu.toObject();
}

static ScreenScraperMetadata parseMetadata(const QByteArray &data)
{
    ScreenScraperMetadata out;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject jeu = responseGameObject(doc);
    if (jeu.isEmpty())
        return out;

    QString title = findLocalizedStringRecursive(jeu, QStringList()
                                                 << QStringLiteral("nom_us")
                                                 << QStringLiteral("nom_wor")
                                                 << QStringLiteral("nom_en")
                                                 << QStringLiteral("nom")
                                                 << QStringLiteral("title"));
    if (title.isEmpty())
        title = findLocalizedStringRecursive(jeu, QStringList() << QStringLiteral("jeu") << QStringLiteral("name"));

    QString description = findLocalizedStringRecursive(jeu, QStringList()
                                                       << QStringLiteral("synopsis_us")
                                                       << QStringLiteral("synopsis_en")
                                                       << QStringLiteral("synopsis_wor")
                                                       << QStringLiteral("synopsis")
                                                       << QStringLiteral("description")
                                                       << QStringLiteral("resume"));

    const QString coverUrl = findCoverUrlRecursive(jeu);
    if (title.isEmpty() && description.isEmpty() && coverUrl.isEmpty())
        return out;

    out.success = true;
    out.title = title.simplified();
    out.description = description.simplified();
    out.source = QStringLiteral("ScreenScraper");

    if (!coverUrl.isEmpty()) {
        const QString ext = fileExtFromUrl(coverUrl);
        const QString localPath = QDir(coversPath()).filePath(hashedFileName(title + QStringLiteral("|") + coverUrl, ext));
        if (!QFileInfo(localPath).exists()) {
            QByteArray imageData;
            if (httpGet(QUrl(coverUrl), &imageData)) {
                QFile f(localPath);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(imageData);
                    f.close();
                }
            }
        }
        if (QFileInfo(localPath).exists())
            out.coverPath = localPath;
    }

    return out;
}

static QUrl buildGameInfoUrl(const ApiConfig &cfg, const QString &systemId,
                             const QString &queryTitle, const QString &fallbackFileName,
                             qint64 fileSize, const FileHashes *hashes)
{
    QUrl url(QStringLiteral("https://api.screenscraper.fr/api2/jeuInfos.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("devid"), cfg.devId);
    query.addQueryItem(QStringLiteral("devpassword"), cfg.devPassword);
    query.addQueryItem(QStringLiteral("softname"), cfg.softwareName);
    query.addQueryItem(QStringLiteral("ssid"), cfg.user);
    query.addQueryItem(QStringLiteral("sspassword"), cfg.password);
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("romtype"), QStringLiteral("rom"));
    if (!systemId.isEmpty())
        query.addQueryItem(QStringLiteral("systemeid"), systemId);
    if (fileSize > 0)
        query.addQueryItem(QStringLiteral("romtaille"), QString::number(fileSize));

    const QString romName = normalizeRomNameForQuery(queryTitle, fallbackFileName);
    query.addQueryItem(QStringLiteral("romnom"), romName);

    if (hashes) {
        if (!hashes->crc32.isEmpty())
            query.addQueryItem(QStringLiteral("crc"), hashes->crc32);
        if (!hashes->md5.isEmpty())
            query.addQueryItem(QStringLiteral("md5"), hashes->md5);
        if (!hashes->sha1.isEmpty())
            query.addQueryItem(QStringLiteral("sha1"), hashes->sha1);
    }

    url.setQuery(query);
    return url;
}

} // namespace

bool ScreenScraperClient::isConfigured()
{
    return hasApiConfig(readConfig());
}

ScreenScraperMetadata ScreenScraperClient::fetchMetadata(const QString &romPath,
                                                         const QString &system,
                                                         const QString &headerTitle,
                                                         const QString &internalId)
{
    ScreenScraperMetadata out;
    const ApiConfig cfg = readConfig();
    if (!hasApiConfig(cfg))
        return out;

    const QFileInfo fi(romPath);
    const QString base = fi.completeBaseName();
    const QString ssSystemId = systemIdForName(system);

    QByteArray responseData;
    QStringList nameCandidates;
    const QString headerCandidate = firstUsefulTitle(QStringList() << headerTitle << internalId);
    if (!headerCandidate.isEmpty())
        nameCandidates << headerCandidate;
    const QString fileCandidate = firstUsefulTitle(QStringList() << base);
    if (!fileCandidate.isEmpty() && !nameCandidates.contains(fileCandidate, Qt::CaseInsensitive))
        nameCandidates << fileCandidate;

    for (int i = 0; i < nameCandidates.size(); ++i) {
        const QUrl url = buildGameInfoUrl(cfg, ssSystemId, nameCandidates.at(i), fi.fileName(), fi.size(), 0);
        if (!httpGet(url, &responseData) || responseData.isEmpty())
            continue;
        if (responseData.contains("API closed") || responseData.contains("Erreur"))
            continue;
        out = parseMetadata(responseData);
        if (out.success)
            return out;
    }

    const FileHashes hashes = computeFileHashes(romPath);
    if (!hashes.crc32.isEmpty() || !hashes.md5.isEmpty() || !hashes.sha1.isEmpty()) {
        const QString queryName = !headerCandidate.isEmpty() ? headerCandidate : fileCandidate;
        const QUrl url = buildGameInfoUrl(cfg, ssSystemId, queryName, fi.fileName(), fi.size(), &hashes);
        if (httpGet(url, &responseData) && !responseData.isEmpty() &&
            !responseData.contains("API closed") && !responseData.contains("Erreur")) {
            out = parseMetadata(responseData);
            if (out.success)
                return out;
        }
    }

    return out;
}
