#pragma once

#include <QVector>
#include <QWidget>

class QPainter;
class QLabel;
class QWheelEvent;

class CurvePlot final : public QWidget
{
    Q_OBJECT
public:
    explicit CurvePlot(QWidget *parent = nullptr);
    void clear();
    void addSample(quint32 sequence, quint32 ms, int pressure, int ntc0, int ntc1, int valid);
    void addEvent(quint32 ms, const QString &label);
    bool savePng(const QString &path, bool includeHidden = false) const;
    bool canExportSelection() const { return !samples.isEmpty() && visibleChannels; }

signals:
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct Sample {
        quint32 sequence;
        qint64 ms;
        int values[3];
        int valid;
    };
    struct Marker {
        quint32 boardMs;
        QString label;
        qint64 ms;
    };
    qint64 relativeTime(quint32 ms) const;
    void draw(QPainter &painter, const QRect &bounds, bool entireRun, int channelMask) const;
    void updateIntervalLabel();

    QVector<Sample> samples;
    QVector<Marker> markers;
    quint32 lastBoardMs = 0;
    int visibleChannels = 7;
    int intervalIndex;
    int wheelRemainder = 0;
    QLabel *intervalLabel;
};
