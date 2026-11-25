#pragma once
#include "protocol.h"
#include <QDir>
#include <QFile>

class Archive {
public:
    bool create(const QString &base, quint32 run, const QByteArray &cfg);
    bool reopen(const QString &folder, quint32 run);
    bool sample(const Protocol::Point &p);
    bool event(const Protocol::Point &p);
    bool note(const QString &message);
    void close();
    bool active() const { return samples.isOpen() && events.isOpen() && notes.isOpen(); }
    QString directory;
    QString error;
    quint32 ack = 0;
    bool sawTerminal = false, partial = false;
private:
    bool openFiles(QIODevice::OpenMode mode);
    bool write(QFile &file, const QByteArray &line);
    QFile samples, events, notes;
    bool legacyEvents = false;
    std::optional<quint32> openedAt;
};
