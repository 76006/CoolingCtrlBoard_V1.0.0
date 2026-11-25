#include "curveplot.h"
#include "protocol.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace {
constexpr int validBits[] = {4, 1, 2};
const QColor colors[] = {QColor("#12679a"), QColor("#bf5624"), QColor("#238366")};
const QString names[] = {QStringLiteral("压力"), QStringLiteral("泄压口 NTC0"), QStringLiteral("罐体 NTC1")};
constexpr int tickIntervalsMs[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000,
                                   30000, 60000, 120000, 300000, 600000, 1800000, 3600000};
constexpr int defaultIntervalIndex = 7;
static_assert(tickIntervalsMs[defaultIntervalIndex] == 20000);

struct Range {
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    void add(double value) { low = std::min(low, value); high = std::max(high, value); }
    void expand(double minimumSpan)
    {
        if (!std::isfinite(low)) { low = 0; high = minimumSpan; }
        const double middle = (low + high) * 0.5;
        const double half = std::max(minimumSpan, (high - low) * 1.12) * 0.5;
        low = middle - half;
        high = middle + half;
    }
};

QString elapsedLabel(double seconds, int precision)
{
    if (seconds < 120.0)
        return QString::number(seconds, 'f', precision) + QStringLiteral(" s");
    const auto tenths = qRound64(seconds * 10);
    const auto total = precision ? tenths / 10 : qRound64(seconds);
    QString label = QStringLiteral("%1:%2:%3").arg(total / 3600)
        .arg(total / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
    if (precision) label += QStringLiteral(".%1").arg(tenths % 10);
    return label;
}
}

CurvePlot::CurvePlot(QWidget *parent) : QWidget(parent), intervalIndex(defaultIntervalIndex)
{
    setMinimumSize(320, 190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("压力与温度同步曲线"));
    auto *controls = new QHBoxLayout(this);
    controls->setContentsMargins(8, 4, 8, 0);
    controls->setAlignment(Qt::AlignTop);
    for (int ch = 0; ch < 3; ++ch) {
        auto *box = new QCheckBox(names[ch], this);
        box->setChecked(true);
        box->setStyleSheet(QStringLiteral("QCheckBox { color: %1; }").arg(colors[ch].name()));
        controls->addWidget(box, 0, Qt::AlignTop);
        connect(box, &QCheckBox::toggled, this, [this, ch](bool checked) {
            if (checked) visibleChannels |= validBits[ch];
            else visibleChannels &= ~validBits[ch];
            emit selectionChanged();
            update();
        });
    }
    controls->addStretch();
    intervalLabel = new QLabel(this);
    controls->addWidget(intervalLabel, 0, Qt::AlignTop);
    setToolTip(tr("在图内向上滚动缩短每格时间，向下滚动延长每格时间"));
    updateIntervalLabel();
}

void CurvePlot::updateIntervalLabel()
{
    const int ms = tickIntervalsMs[intervalIndex];
    intervalLabel->setText(ms < 60000 ? tr("%1 秒/格").arg(ms / 1000.0)
                                     : tr("%1 分钟/格").arg(ms / 60000));
}

void CurvePlot::wheelEvent(QWheelEvent *event)
{
    const int angle = event->angleDelta().y();
    const int delta = angle ? angle : event->pixelDelta().y() * 3;
    if (!delta) { event->ignore(); return; }
    wheelRemainder += delta;
    const int steps = wheelRemainder / 120;
    wheelRemainder %= 120;
    if (steps) {
        intervalIndex = std::clamp(intervalIndex - steps, 0, int(std::size(tickIntervalsMs)) - 1);
        updateIntervalLabel();
        update();
    }
    event->accept();
}

void CurvePlot::clear()
{
    samples.clear();
    markers.clear();
    lastBoardMs = 0;
    update();
}

qint64 CurvePlot::relativeTime(quint32 ms) const
{
    // Events may be replayed a few frames behind the newest sample.
    return samples.isEmpty() ? 0 : samples.back().ms + qint32(ms - lastBoardMs);
}

void CurvePlot::addSample(quint32 sequence, quint32 ms, int pressure, int ntc0, int ntc1, int valid)
{
    const bool first = samples.isEmpty();
    if (!first && (samples.back().sequence == sequence || qint32(ms - lastBoardMs) < 0))
        return;
    const qint64 time = relativeTime(ms);
    samples.append({sequence, time, {pressure, ntc0, ntc1}, valid});
    lastBoardMs = ms;
    if (first) {
        for (auto &marker : markers)
            marker.ms = relativeTime(marker.boardMs);
    }
    update();
}

void CurvePlot::addEvent(quint32 ms, const QString &label)
{
    markers.append({ms, label, relativeTime(ms)});
    update();
}

void CurvePlot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    draw(painter, rect(), false, visibleChannels);
}

bool CurvePlot::savePng(const QString &path, bool includeHidden) const
{
    if (samples.isEmpty() || (!includeHidden && !visibleChannels))
        return false;
    QImage image(1600, 800, QImage::Format_RGB32);
    QPainter painter(&image);
    draw(painter, image.rect(), true, includeHidden ? 7 : visibleChannels);
    painter.end();
    return image.save(path, "PNG");
}

void CurvePlot::draw(QPainter &p, const QRect &bounds, bool entireRun, int channelMask) const
{
    const QColor ink("#293d4b");
    p.fillRect(bounds, Qt::white);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font = p.font();
    font.setPixelSize(entireRun ? 17 : 12);
    p.setFont(font);
    const QFontMetrics fm(font);
    const double top = entireRun ? fm.height() * 2.8 : layout()->sizeHint().height() + fm.height() * 1.8;
    const QRectF graph = QRectF(bounds).adjusted(entireRun ? 92 : 54, top,
                                                 entireRun ? -90 : -48, -fm.height() * 2.4);
    int legendX = int(graph.left());
    for (int channel = 0; entireRun && channel < 3; ++channel) {
        if (!(channelMask & validBits[channel])) continue;
        p.setPen(QPen(colors[channel], 3));
        p.drawLine(legendX, fm.height(), legendX + 16, fm.height());
        p.setPen(ink);
        p.drawText(legendX + 21, fm.height() + fm.ascent() / 3, names[channel]);
        legendX += fm.horizontalAdvance(names[channel]) + 34;
    }
    if (samples.isEmpty() || !channelMask) {
        p.drawText(graph, Qt::AlignCenter, channelMask ? tr("等待采样") : tr("请勾选要显示的曲线"));
        return;
    }

    qint64 firstTime = 0;
    qint64 end = samples.back().ms;
    for (const auto &marker : markers) {
        firstTime = std::min(firstTime, marker.ms);
        end = std::max(end, marker.ms);
    }
    const int ticks = std::clamp(int(graph.width()/105), 2, 8);
    const qint64 interval = tickIntervalsMs[intervalIndex];
    const qint64 start = entireRun ? firstTime : std::max(firstTime, end - ticks * interval);
    end = entireRun ? std::max<qint64>(end, firstTime + 1000) : start + ticks * interval;
    auto begin = std::lower_bound(samples.cbegin(), samples.cend(), start,
        [](const Sample &s, qint64 ms) { return s.ms < ms; });
    const auto x = [&](qint64 ms) {
        return graph.left() + graph.width() * double(ms - start) / double(end - start);
    };

    Range ranges[2];
    bool hasEstimatedOutlet = false;
    for (auto it = begin; it != samples.cend(); ++it) {
        const int shown = it->valid & channelMask;
        if ((shown & validBits[1]) && it->values[1] < Protocol::OutletCalibratedMinDeg10)
            hasEstimatedOutlet = true;
        for (int ch = 0; ch < 3; ++ch) {
            if (shown & validBits[ch])
                ranges[ch != 0].add(it->values[ch]);
        }
    }
    ranges[0].expand(20);
    if (ranges[0].low < 0) ranges[0].low = 0;
    ranges[1].expand(10);
    p.setPen(ink);
    if (hasEstimatedOutlet) {
        p.drawText(QRectF(graph.left(), graph.top() - 1.45 * fm.height(), graph.width(), fm.height()),
                   Qt::AlignCenter, QStringLiteral("NTC0 低于 −40 ℃ 为估算"));
    }
    const bool showPressure = channelMask & validBits[0];
    const bool showTemperature = channelMask & (validBits[1] | validBits[2]);
    if (showPressure)
        p.drawText(QRectF(bounds.left() + 5, graph.top() - 1.65 * fm.height(), 48, fm.height()), "MPa");
    if (showTemperature)
        p.drawText(QRectF(graph.right() + 7, graph.top() - 1.65 * fm.height(), 48, fm.height()), QStringLiteral("°C"));
    for (int i = 0; i <= 4; ++i) {
        const double frac = i / 4.0;
        const double y = graph.bottom() - frac * graph.height();
        p.setPen(QColor("#dee5ea"));
        p.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
        p.setPen(ink);
        const auto axisText = [&](const Range &range, double scale, int digits) {
            return QString::number((range.low + frac * (range.high - range.low)) / scale, 'f', digits);
        };
        if (showPressure)
            p.drawText(QRectF(bounds.left(), y - fm.height()/2.0, graph.left()-6, fm.height()),
                       Qt::AlignRight | Qt::AlignVCenter, axisText(ranges[0], 1000, 3));
        if (showTemperature)
            p.drawText(QRectF(graph.right()+5, y - fm.height()/2.0, 80, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, axisText(ranges[1], 10, 1));
    }
    const qint64 firstTick = start - start % interval + (start % interval > 0 ? interval : 0);
    const bool fractionalLabels = entireRun ? end - start < 10000 : interval < 1000;
    for (int i = 0; i <= ticks; ++i) {
        const qint64 ms = entireRun ? start + (end - start) * i / ticks : firstTick + i * interval;
        if (ms > end) break;
        p.setPen(QColor("#dee5ea"));
        p.drawLine(QPointF(x(ms), graph.top()), QPointF(x(ms), graph.bottom()));
        p.setPen(ink);
        p.drawText(QRectF(x(ms)-46, graph.bottom()+4, 92, fm.height()),
                   Qt::AlignHCenter, elapsedLabel(ms / 1000.0, fractionalLabels ? 1 : 0));
    }

    p.save();
    p.setClipRect(graph.adjusted(-1,-2,1,1));
    // Collapse a dense pixel column to its envelope so narrow pressure peaks
    // survive a full-run PNG without allocating a path per source sample.
    for (int ch = 0; ch < 3; ++ch) {
        if (!(channelMask & validBits[ch])) continue;
        const Range &range = ranges[ch != 0];
        p.setPen(QPen(colors[ch], entireRun ? 2.2 : 1.7));
        QPainterPath path;
        int column = -1;
        double previous = 0, ymin = 0, ymax = 0;
        bool connected = false;
        auto flush = [&]() {
            if (column < 0) return;
            path.lineTo(column, ymin);
            path.lineTo(column, ymax);
            path.lineTo(column, previous);
            column = -1;
        };
        quint32 lastSeq = 0;
        for (auto it = begin; it != samples.cend(); ++it) {
            if (!(it->valid & validBits[ch]) || (connected && it->sequence != lastSeq + 1U)) {
                flush();
                connected = false;
            }
            if (!(it->valid & validBits[ch])) continue;
            const int px = qRound(x(it->ms));
            const double y = graph.bottom() - (it->values[ch] - range.low) *
                                    graph.height() / (range.high - range.low);
            if (px != column) {
                flush();
                if (connected) path.lineTo(px, y);
                else { path.moveTo(px, y); p.drawPoint(QPointF(px, y)); }
                column = px;
                ymin = ymax = y;
            }
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
            previous = y;
            connected = true;
            lastSeq = it->sequence;
        }
        flush();
        p.drawPath(path);
    }
    double labelEnd[] = {-1000, -1000, -1000};
    for (const auto &marker : markers) {
        if (marker.ms < start || marker.ms > end) continue;
        const double px = x(marker.ms);
        p.setPen(QPen(QColor("#788590"), 1, Qt::DashLine));
        p.drawLine(QPointF(px,graph.top()), QPointF(px,graph.bottom()));
        const int width = fm.horizontalAdvance(marker.label) + 8;
        const double left = std::min(px+2, graph.right()-width);
        for (int row = 0; row < 3; ++row) {
            if (left <= labelEnd[row]) continue;
            QRectF label(left,graph.top()+2+row*(fm.height()+2),width,fm.height()+1);
            p.fillRect(label, QColor(246,248,250,230));
            p.setPen(ink);
            p.drawText(label,Qt::AlignCenter,marker.label);
            labelEnd[row] = label.right()+3;
            break;
        }
    }
    p.restore();
}
