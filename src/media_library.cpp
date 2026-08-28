#include "media_library.h"
#include "kadia_settings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QWidget>
#include <algorithm>
#include <string>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace {

QString settingsKey(KadiaMediaKind kind)
{
    switch (kind) {
    case MediaMusic: return QStringLiteral("media/musicFolders");
    case MediaVideo: return QStringLiteral("media/videoFolders");
    case MediaPicture: return QStringLiteral("media/pictureFolders");
    case MediaRecordedTV: return QStringLiteral("media/recordedTvFolders");
    default: return QString();
    }
}

QStringList normalizedExisting(const QStringList &input)
{
    QStringList out;
    QSet<QString> seen;
    for (int i = 0; i < input.size(); ++i) {
        const QFileInfo info(input.at(i));
        if (!info.exists() || !info.isDir())
            continue;
        const QString absolute = QDir::cleanPath(info.absoluteFilePath());
        const QString key = absolute.toLower();
        if (!seen.contains(key)) {
            seen.insert(key);
            out << absolute;
        }
    }
    return out;
}

bool hasExtension(const QString &suffix, const char * const *extensions, int count)
{
    for (int i = 0; i < count; ++i) {
        if (suffix.compare(QString::fromLatin1(extensions[i]), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

KadiaMediaKind kindForFile(const QFileInfo &info, bool recordedRoot)
{
    static const char *music[] = {"mp3","flac","m4a","aac","ogg","opus","wav","wma","ape","mpc","aif","aiff","alac"};
    static const char *video[] = {"mp4","mkv","avi","mov","m4v","wmv","mpg","mpeg","ts","m2ts","mts","webm","flv","vob","ogv"};
    static const char *picture[] = {"jpg","jpeg","png","bmp","gif","tif","tiff","webp"};
    static const char *recorded[] = {"wtv","dvr-ms"};
    const QString suffix = info.suffix().toLower();
    if (hasExtension(suffix, recorded, sizeof(recorded)/sizeof(recorded[0])))
        return MediaRecordedTV;
    if (hasExtension(suffix, music, sizeof(music)/sizeof(music[0])))
        return MediaMusic;
    if (hasExtension(suffix, picture, sizeof(picture)/sizeof(picture[0])))
        return MediaPicture;
    if (hasExtension(suffix, video, sizeof(video)/sizeof(video[0])))
        return recordedRoot ? MediaRecordedTV : MediaVideo;
    return MediaAny;
}

bool accepts(KadiaMediaKind requested, KadiaMediaKind actual)
{
    return requested == MediaAny || requested == actual;
}

QString playlistPath(KadiaMediaKind kind)
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        root = QDir::homePath() + QStringLiteral("/.kadia");
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("kadia-%1.m3u").arg(static_cast<int>(kind)));
}

}

QString MediaLibrary::kindLabel(KadiaMediaKind kind)
{
    switch (kind) {
    case MediaMusic: return QStringLiteral("Music");
    case MediaVideo: return QStringLiteral("Videos + Movies");
    case MediaPicture: return QStringLiteral("Pictures");
    case MediaRecordedTV: return QStringLiteral("Recorded TV");
    default: return QStringLiteral("Media");
    }
}

QStringList MediaLibrary::defaultFolders(KadiaMediaKind kind)
{
    QStringList out;
    if (kind == MediaMusic || kind == MediaAny) {
        const QString p = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!p.isEmpty()) out << p;
    }
    if (kind == MediaVideo || kind == MediaAny) {
        const QString p = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (!p.isEmpty()) out << p;
    }
    if (kind == MediaPicture || kind == MediaAny) {
        const QString p = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (!p.isEmpty()) out << p;
    }
    if (kind == MediaRecordedTV || kind == MediaAny) {
        out << QDir::home().filePath(QStringLiteral("Recorded TV"));
#ifdef Q_OS_WIN
        const QString publicRoot = QString::fromLocal8Bit(qgetenv("PUBLIC"));
        if (!publicRoot.isEmpty()) out << QDir(publicRoot).filePath(QStringLiteral("Recorded TV"));
#endif
    }
    return normalizedExisting(out);
}

QStringList MediaLibrary::folders(KadiaMediaKind kind)
{
    if (kind == MediaAny) {
        QStringList all;
        all << folders(MediaMusic) << folders(MediaVideo) << folders(MediaPicture) << folders(MediaRecordedTV);
        return normalizedExisting(all);
    }

    QSettings settings(KadiaSettings::settingsPath(), QSettings::IniFormat);
    const QString key = settingsKey(kind);
    const QStringList custom = settings.value(key).toStringList();
    if (!custom.isEmpty())
        return normalizedExisting(custom);
    return defaultFolders(kind);
}

void MediaLibrary::setFolders(KadiaMediaKind kind, const QStringList &newFolders)
{
    if (kind == MediaAny)
        return;
    QSettings settings(KadiaSettings::settingsPath(), QSettings::IniFormat);
    settings.setValue(settingsKey(kind), normalizedExisting(newFolders));
    settings.sync();
}

QVector<KadiaMediaItem> MediaLibrary::scan(KadiaMediaKind kind, const QAtomicInteger<int> *cancel)
{
    QVector<KadiaMediaItem> items;
    QSet<QString> seenFiles;

    QVector<QPair<QString, bool> > roots;
    if (kind == MediaAny) {
        const KadiaMediaKind kinds[] = {MediaMusic, MediaVideo, MediaPicture, MediaRecordedTV};
        for (int k = 0; k < 4; ++k) {
            const QStringList list = folders(kinds[k]);
            for (int i = 0; i < list.size(); ++i)
                roots.push_back(qMakePair(list.at(i), kinds[k] == MediaRecordedTV));
        }
    } else {
        const QStringList list = folders(kind);
        for (int i = 0; i < list.size(); ++i)
            roots.push_back(qMakePair(list.at(i), kind == MediaRecordedTV));
    }

    for (int r = 0; r < roots.size(); ++r) {
        if (cancel && cancel->loadAcquire() != 0)
            break;
        QDirIterator it(roots.at(r).first,
                        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancel && cancel->loadAcquire() != 0)
                break;
            const QString path = it.next();
            const QFileInfo info = it.fileInfo();
            const KadiaMediaKind actual = kindForFile(info, roots.at(r).second);
            if (actual == MediaAny || !accepts(kind, actual))
                continue;
            const QString absolute = QDir::cleanPath(info.absoluteFilePath());
            const QString dedupe = absolute.toLower();
            if (seenFiles.contains(dedupe))
                continue;
            seenFiles.insert(dedupe);

            KadiaMediaItem item;
            item.path = absolute;
            item.title = info.completeBaseName();
            item.kind = actual;
            item.sizeBytes = info.size();
            item.modified = info.lastModified();
            items.push_back(item);
        }
    }

    std::sort(items.begin(), items.end(), [](const KadiaMediaItem &a, const KadiaMediaItem &b) {
        const int title = a.title.compare(b.title, Qt::CaseInsensitive);
        if (title != 0) return title < 0;
        return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });
    return items;
}

bool MediaLibrary::openPath(const QString &path, QWidget *parent, QString *error)
{
    Q_UNUSED(parent);
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error) *error = QStringLiteral("The media file no longer exists: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const std::wstring working = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
    const HINSTANCE result = ShellExecuteW(0, L"open", native.c_str(), 0, working.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        if (error) *error = QStringLiteral("Windows could not open this media file with its associated player.");
        return false;
    }
    return true;
#else
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
        if (error) *error = QStringLiteral("The desktop media handler could not open this file.");
        return false;
    }
    return true;
#endif
}

bool MediaLibrary::playItems(const QVector<KadiaMediaItem> &items, QWidget *parent, QString *error)
{
    QVector<KadiaMediaItem> playable;
    KadiaMediaKind playlistKind = MediaAny;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).kind != MediaPicture) {
            if (playlistKind == MediaAny) playlistKind = items.at(i).kind;
            playable.push_back(items.at(i));
        }
    }
    if (playable.isEmpty()) {
        if (error) *error = QStringLiteral("No playable media was found in this library.");
        return false;
    }

    const QString path = playlistPath(playlistKind);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Kadia could not create the temporary playlist.");
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QChar(0xFEFF) << "#EXTM3U\n";
    for (int i = 0; i < playable.size(); ++i)
        stream << playable.at(i).path << "\n";
    file.close();
    return openPath(path, parent, error);
}

bool MediaLibrary::playAll(KadiaMediaKind kind, QWidget *parent, QString *error)
{
    return playItems(scan(kind), parent, error);
}
