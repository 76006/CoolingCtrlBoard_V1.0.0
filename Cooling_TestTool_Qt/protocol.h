#pragma once
#include <QByteArray>
#include <QString>
#include <array>
#include <optional>

namespace Protocol {
struct Setting {
    const char *label;
    const char *unit;
    int scale, low, high;
    const char *initial;
};
extern const std::array<Setting, 12> settings;
struct Point {
    quint32 run, number, ms;
    int round;
    QString code, stage;
    int p, t0, t1, valid = 0, heat = 0, peak = 0;
};
bool unsignedField(const QByteArray &s, quint32 &value);
bool signedField(const QByteArray &s, int &value);
std::optional<Point> point(const QList<QByteArray> &fields, bool event);
QString stateName(const QString &state);
QString eventName(const QString &code);
bool terminalEvent(const QString &code);
QString pressure(int p);
QString temperature(int t);
}
