#include "curveplot.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int validBits[] = {4, 1, 2};
const QColor colors[] = {QColor("#12679a"), QColor("#bf5624"), QColor("#238366")};
const QString names[] = {QStringLiteral("压力"), QStringLiteral("泄压口 NTC0"), QStringLiteral("罐体 NTC1")};

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
    const auto total = qRound64(seconds);
    return QStringLiteral("%1:%2:%3").arg(total / 3600)
        .arg(total / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}
}

CurvePlot::CurvePlot(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(320, 190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("压力与温度同步曲线"));
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
    draw(painter, rect(), false);
}

bool CurvePlot::savePng(const QString &path) const
{
    if (samples.isEmpty())
        return false;
    QImage image(1600, 800, QImage::Format_RGB32);
    QPainter painter(&image);
    draw(painter, image.rect(), true);
    painter.end();
    return image.save(path, "PNG");
}

void CurvePlot::draw(QPainter &p, const QRect &bounds, bool entireRun) const
{
    const QColor ink("#293d4b");
    p.fillRect(bounds, Qt::white);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font = p.font();
    font.setPixelSize(entireRun ? 17 : 12);
    p.setFont(font);
    const QFontMetrics fm(font);
    const QRectF graph = QRectF(bounds).adjusted(entireRun ? 92 : 54, fm.height() * 2.8,
                                                 entireRun ? -90 : -48, -fm.height() * 2.4);
    int legendX = int(graph.left());
    for (int channel = 0; channel < 3; ++channel) {
        p.setPen(QPen(colors[channel], 3));
        p.drawLine(legendX, fm.height(), legendX + 16, fm.height());
        p.setPen(ink);
        p.drawText(legendX + 21, fm.height() + fm.ascent() / 3, names[channel]);
        legendX += fm.horizontalAdvance(names[channel]) + 34;
    }
    if (samples.isEmpty()) {
        p.drawText(graph, Qt::AlignCenter, QStringLiteral("等待采样"));
        return;
    }

    qint64 firstTime = 0;
    qint64 end = std::max<qint64>(1000, samples.back().ms);
    for (const auto &marker : markers) {
        firstTime = std::min(firstTime, marker.ms);
        end = std::max(end, marker.ms);
    }
    const qint64 start = entireRun ? firstTime : std::max(firstTime, end - 120000);
    auto begin = std::lower_bound(samples.cbegin(), samples.cend(), start,
        [](const Sample &s, qint64 ms) { return s.ms < ms; });
    const auto x = [&](qint64 ms) {
        return graph.left() + graph.width() * double(ms - start) / double(end - start);
    };

    Range ranges[2];
    for (auto it = begin; it != samples.cend(); ++it) {
        for (int ch = 0; ch < 3; ++ch) {
            if (it->valid & validBits[ch])
                ranges[ch != 0].add(it->values[ch]);
        }
    }
    ranges[0].expand(20);
    if (ranges[0].low < 0) ranges[0].low = 0;
    ranges[1].expand(10);
    p.setPen(ink);
    p.drawText(QRectF(bounds.left() + 5, graph.top() - 1.65 * fm.height(), 48, fm.height()), "MPa");
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
        p.drawText(QRectF(bounds.left(), y - fm.height()/2.0, graph.left()-6, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, axisText(ranges[0], 1000, 3));
        p.drawText(QRectF(graph.right()+5, y - fm.height()/2.0, 80, fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, axisText(ranges[1], 10, 1));
    }
    const int ticks = std::clamp(int(graph.width()/105), 2, 8);
    for (int i = 0; i <= ticks; ++i) {
        const qint64 ms = start + (end - start) * i / ticks;
        p.drawText(QRectF(x(ms)-46, graph.bottom()+4, 92, fm.height()),
                   Qt::AlignHCenter, elapsedLabel(ms / 1000.0, end-start < 10000 ? 1 : 0));
    }

    p.save();
    p.setClipRect(graph.adjusted(-1,-2,1,1));
    // Collapse a dense pixel column to its envelope so narrow pressure peaks
    // survive a full-run PNG without allocating a path per source sample.
    for (int ch = 0; ch < 3; ++ch) {
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
