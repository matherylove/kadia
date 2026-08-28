#pragma once

#include <QAtomicInteger>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

enum KadiaMediaKind {
    MediaMusic = 0,
    MediaVideo = 1,
    MediaPicture = 2,
    MediaRecordedTV = 3,
    MediaAny = 4
};

struct KadiaMediaItem {
    QString path;
    QString title;
    KadiaMediaKind kind;
    qint64 sizeBytes;
    QDateTime modified;

    KadiaMediaItem() : kind(MediaAny), sizeBytes(0) {}
};

class MediaLibrary
{
public:
    static QString kindLabel(KadiaMediaKind kind);
    static QStringList folders(KadiaMediaKind kind);
    static QStringList defaultFolders(KadiaMediaKind kind);
    static void setFolders(KadiaMediaKind kind, const QStringList &folders);

    static QVector<KadiaMediaItem> scan(KadiaMediaKind kind, const QAtomicInteger<int> *cancel = 0);
    static bool openPath(const QString &path, QWidget *parent = 0, QString *error = 0);
    static bool playItems(const QVector<KadiaMediaItem> &items, QWidget *parent = 0, QString *error = 0);
    static bool playAll(KadiaMediaKind kind, QWidget *parent = 0, QString *error = 0);
};
