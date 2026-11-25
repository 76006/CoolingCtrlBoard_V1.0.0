#include "protocol.h"
#include <QHash>
#include <QList>

namespace Protocol {
const std::array<Setting, 12> settings {{
    {"最高压力","MPa",1000,1,1599,""}, {"初始压力","MPa",1000,0,1599,""},
    {"回压容差","MPa",1000,1,999,""}, {"温降观察窗口","s",10,1,100,""},
    {"触发降幅","℃",10,1,1000,""}, {"开始确认时长","s",10,1,600,""},
    {"停止回升幅度","℃",10,1,1000,""}, {"停止确认时长","s",10,1,600,""},
    {"压力稳定时长","s",10,1,600,""}, {"达上限观察时长","s",10,1,6000,""},
    {"单轮最长时长","s",10,1,864000,""}, {"试验轮数","轮",1,1,999,"1"}
}};

bool unsignedField(const QByteArray &s, quint32 &value)
{
    if (s.isEmpty() || s.size() > 10) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    bool ok = false;
    value = s.toUInt(&ok);
    return ok;
}
bool signedField(const QByteArray &s, int &value)
{
    if (s.isEmpty()) return false;
    quint32 magnitude;
    bool neg = s.front() == '-';
    if (!unsignedField(neg ? s.mid(1) : s, magnitude) || magnitude > 2147483647U) return false;
    value = neg ? -int(magnitude) : int(magnitude);
    return true;
}
static const QHash<QString, QString> states {
    {"IDLE","空闲"},{"BASE","采集初始压力"},{"HEAT","加热升压"},{"LIMIT","达上限观察"},
    {"CHECK","确认温降"},{"VENT","等待回温"},{"RETURN","自然回压"},{"DONE","完成"},{"ABORT","中止"}
};
static const QHash<QString, QString> events {
    {"BASE","初始压力就绪"},{"HEAT","开始加压"},{"LIMIT","达到最高压力"},{"TRIGGER","首次温降触发"},
    {"OPEN","泄压开始（NTC0 判定）"},{"CLOSE","泄压停止（NTC0 判定）"},{"ROUND","回压完成"},
    {"DONE","试验完成"},{"FALSE_DROP","温降未确认"},{"NO_RELIEF","达上限未检出泄压"},
    {"TIMEOUT","试验超时"},{"BAD_BASE","初始压力不合格"},{"STOP","人工停止"},
    {"LINK","通信中断"},{"SENSOR","传感器异常"},{"INTERLOCK","联锁停机"},
    {"OVERPRESS","超压停机"},{"OVERTEMP","超温停机"},{"OVERFLOW","事件缓存已满"}
};
QString stateName(const QString &s) { return states.value(s); }
QString eventName(const QString &s) { return events.value(s); }
bool terminalEvent(const QString &s)
{
    return events.contains(s) && s!="BASE" && s!="HEAT" && s!="LIMIT" && s!="TRIGGER" &&
           s!="OPEN" && s!="CLOSE" && s!="ROUND";
}
std::optional<Point> point(const QList<QByteArray> &f, bool event)
{
    Point v{};
    if (f.size() != (event ? 13 : 12) || !unsignedField(f[2], v.run) ||
        !unsignedField(f[3], v.number) || !unsignedField(f[4], v.ms) ||
        !signedField(f[5], v.round) || v.round<0 || v.round>999) return {};
    v.code = QString::fromLatin1(f[6]);
    if ((event ? !events.contains(v.code) : !states.contains(v.code)) ||
        !signedField(f[7], v.p) || v.p<0 || v.p>1600 ||
        !signedField(f[8], v.t0) || !signedField(f[9], v.t1) ||
        v.t0 < -32768 || v.t0>32767 || v.t1 < -32768 || v.t1>32767) return {};
    if (event) {
        v.stage=QString::fromLatin1(f[11]);
        if (!v.number || !states.contains(v.stage) || !signedField(f[10], v.peak) ||
            v.peak<0 || v.peak>1600 || !signedField(f[12],v.valid) || v.valid<0 || v.valid>7) return {};
    } else if (!signedField(f[10], v.valid) || v.valid<0 || v.valid>7 ||
               !signedField(f[11], v.heat) || (v.heat != 0 && v.heat != 1)) return {};
    return v;
}
QString pressure(int p) { return QString::number(p / 1000., 'f', 3); }
QString temperature(int t) { return QString::number(t / 10., 'f', 1); }
}
