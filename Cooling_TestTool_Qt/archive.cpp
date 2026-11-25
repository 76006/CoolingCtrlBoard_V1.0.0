#include "archive.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
const QByteArray oldEventHeader = "run,event_id,board_ms,round,event,pressure_MPa,ntc0_C,ntc1_C,peak_MPa,duration_s,result";
const QByteArray eventHeader = oldEventHeader + ",stage,valid_mask";
// Old records have no channel validity or stage. Leave these fields unknown.
bool expandLegacyEvents(QFile &source, QString &error)
{
    QSaveFile replacement(source.fileName());
    if (!replacement.open(QIODevice::WriteOnly)) { error=replacement.errorString(); return false; }
    auto append=[&](const QByteArray &row) {
        if (replacement.write(row)==row.size()) return true;
        error=replacement.errorString();
        return false;
    };
    if (!append(eventHeader+'\n')) return false;
    while (!source.atEnd()) {
        QByteArray line=source.readLine();
        if (!line.endsWith('\n') || line.trimmed().split(',').size()!=11) {
            error=QStringLiteral("旧事件记录不完整，原文件已保留"); return false;
        }
        if (!append(line.trimmed()+",,\n")) return false;
    }
    source.close();
    if (!replacement.commit()) { error=replacement.errorString(); return false; }
    if (!source.open(QIODevice::ReadWrite | QIODevice::ExistingOnly)) { error=source.errorString(); return false; }
    return true;
}
}

bool Archive::write(QFile &file, const QByteArray &line)
{
    if (file.write(line) == line.size() && file.flush()) return true;
    error = file.fileName() + " — " + file.errorString();
    return false;
}
void Archive::close()
{
    samples.close(); events.close(); notes.close();
    legacyEvents = false;
    openedAt.reset();
}
bool Archive::openFiles(QIODevice::OpenMode mode)
{
    samples.setFileName(QDir(directory).filePath("samples.csv"));
    events.setFileName(QDir(directory).filePath("events.csv"));
    notes.setFileName(QDir(directory).filePath("session.log"));
    for (QFile *f : {&samples, &events, &notes}) {
        if (!f->open(mode)) {
            error = f->fileName() + " — " + f->errorString();
            close();
            return false;
        }
    }
    return true;
}
bool Archive::create(const QString &base, quint32 run, const QByteArray &cfg)
{
    close();
    ack = 0; sawTerminal = false; partial = cfg.isEmpty();
    QDir root(base);
    QString name = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_" + QString::number(run);
    if (base.trimmed().isEmpty() || !root.mkpath(".") || !root.mkdir(name)) {
        error = QStringLiteral("无法创建试验目录：") + root.filePath(name);
        return false;
    }
    directory = root.absoluteFilePath(name);
    QJsonObject json{{"run", static_cast<qint64>(run)},{"protocol",2},{"wire_config",QString::fromLatin1(cfg)},
                     {"recovery",partial},{"created",QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}};
    json["ntc0_calibrated_min_C"] = Protocol::OutletCalibratedMinDeg10 / 10.0;
    json["ntc0_below_calibrated_min"] = QStringLiteral("冷端温阻外推估算");
    auto values = cfg.split(',');
    bool fullPower = false;
    if (values.size()==3 || values.size()==Protocol::SettingCount+1)
        fullPower = values.takeLast()=="1";
    const bool manual = values.size()==2;
    if (manual || values.size()==Protocol::SettingCount) {
        json["调温方式"] = fullPower ? QStringLiteral("满功率调温") : QStringLiteral("分段 PWM 调温");
        json["mode"] = manual ? QStringLiteral("手动加热") : QStringLiteral("循环泄压");
        for (int i=0;i<values.size();++i) {
            const auto &f = Protocol::settings[manual && i==1 ? Protocol::MaxTemperatureField : i];
            const QString key = QString::fromUtf8(f.label) + " (" + QString::fromUtf8(f.unit) + ")";
            if (i==1 && values[i]=="-1") json[key] = QStringLiteral("自动实测");
            else json[key] = values[i].toInt()/double(f.scale);
        }
    }
    QFile config(QDir(directory).filePath("parameters.json"));
    QByteArray metadata = QJsonDocument(json).toJson();
    // The session directory is unique. A failed first write prevents START.
    if (!config.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        config.write(metadata)!=metadata.size() || !config.flush()) {
        error = config.errorString(); return false;
    }
    config.close();
    if (!openFiles(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    if (write(samples, "run,adc_seq,board_ms,round,state,pressure_MPa,ntc0_C,ntc1_C,valid_mask,heater_on\n") &&
        write(events,eventHeader+'\n'))
        return true;
    close();
    return false;
}
bool Archive::reopen(const QString &folder, quint32 run)
{
    close();
    directory = folder;
    ack = 0; sawTerminal = false;
    QFile meta(QDir(folder).filePath("parameters.json"));
    if (!meta.open(QIODevice::ReadOnly)) { error=meta.errorString(); return false; }
    const auto metadata=QJsonDocument::fromJson(meta.readAll()).object();
    partial=metadata.value("recovery").toBool();
    if (metadata.value("run").toInteger() != run) {
        error = QStringLiteral("试验目录与设备试验编号不匹配"); return false;
    }
    if (!openFiles(QIODevice::ReadWrite | QIODevice::ExistingOnly)) return false;
    QByteArray header = events.readLine().trimmed();
    legacyEvents=header==oldEventHeader;
    if (!legacyEvents && header!=eventHeader) {
        error = QStringLiteral("事件文件表头无效"); close(); return false;
    }
    while (!events.atEnd()) {
        QByteArray line = events.readLine();
        auto columns = line.trimmed().split(',');
        quint32 id;
        if (!line.endsWith('\n') || columns.size()!=(legacyEvents?11:13) || columns[0]!=QByteArray::number(run) ||
            !Protocol::unsignedField(columns[1],id) || !id || ((ack || !partial) && id!=ack+1)) {
            error = QStringLiteral("事件记录不完整，无法确认设备事件"); close(); return false;
        }
        ack = id;
        sawTerminal = Protocol::terminalEvent(QString::fromLatin1(columns[4]));
        if (columns[4]=="OPEN") openedAt=columns[2].toUInt();
        if (columns[4]=="CLOSE") openedAt.reset();
    }
    if (!samples.size() || !samples.seek(samples.size()-1) || samples.read(1)!="\n") {
        error=QStringLiteral("采样文件尾部不完整，原文件已保留"); close(); return false;
    }
    for (QFile *f : {&samples,&events,&notes})
        if (!f->seek(f->size())) { error=f->errorString(); close(); return false; }
    return true;
}
bool Archive::sample(const Protocol::Point &p)
{
    QList<QByteArray> data{QByteArray::number(p.run),QByteArray::number(p.number),QByteArray::number(p.ms),
        QByteArray::number(p.round),p.code.toLatin1(),
        p.valid & 4 ? Protocol::pressure(p.p).toLatin1() : QByteArray(),
        p.valid & 1 ? Protocol::temperature(p.t0).toLatin1() : QByteArray(),
        p.valid & 2 ? Protocol::temperature(p.t1).toLatin1() : QByteArray(),
        QByteArray::number(p.valid),QByteArray::number(p.heat)};
    return write(samples,data.join(',')+'\n');
}
bool Archive::event(const Protocol::Point &p)
{
    if (legacyEvents) {
        if (!events.seek(0) || events.readLine().trimmed()!=oldEventHeader) {
            error=QStringLiteral("读取旧事件文件失败"); return false;
        }
        if (!expandLegacyEvents(events,error)) return false;
        if (!events.seek(events.size())) { error=events.errorString(); return false; }
        legacyEvents=false;
    }
    QByteArray duration;
    if (p.code=="CLOSE" && openedAt) duration=QByteArray::number(quint32(p.ms-*openedAt)/1000.,'f',3);
    QList<QByteArray> data{QByteArray::number(p.run),QByteArray::number(p.number),QByteArray::number(p.ms),
        QByteArray::number(p.round),p.code.toLatin1(),
        p.valid & 4 ? Protocol::pressure(p.p).toLatin1() : QByteArray(),
        p.valid & 1 ? Protocol::temperature(p.t0).toLatin1() : QByteArray(),
        p.valid & 2 ? Protocol::temperature(p.t1).toLatin1() : QByteArray(),
        p.peak || (p.valid & 4) ? Protocol::pressure(p.peak).toLatin1() : QByteArray(),
        duration,Protocol::eventName(p.code).toUtf8(),p.stage.toLatin1(),QByteArray::number(p.valid)};
    if (!write(events,data.join(',')+'\n')) return false;
    ack = p.number;
    sawTerminal = Protocol::terminalEvent(p.code);
    if (p.code=="OPEN") openedAt=p.ms;
    if (p.code=="CLOSE") openedAt.reset();
    return true;
}
bool Archive::note(const QString &message)
{
    return write(notes,(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)+" "+message+"\n").toUtf8());
}
