#include "../curveplot.h"
#include <QApplication>
#include <QImage>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    CurvePlot plot;
    if (!directory.isValid() || plot.savePng(directory.filePath("empty.png")))
        return 1;

    // Include a board tick rollover, a missing sample, and a delayed event.
    const quint32 start = 0xffff0000U;
    for (quint32 i = 0; i < 2400; ++i) {
        if (i == 1000) continue;
        const bool vent = i >= 700 && i < 720;
        plot.addSample(i, start + 100 * i, vent ? 830 : 300 + int(i % 400),
                       vent ? -15 : 175, 240, i == 1800 ? 4 : 7);
    }
    plot.addEvent(start + 70000, QStringLiteral("检测到泄压"));
    const QString path = directory.filePath("history.png");
    if (!plot.savePng(path))
        return 2;
    QImage image(path);
    if (image.isNull() || image.width() < 1000 || image.height() < 500)
        return 3;
    plot.clear();
    if (plot.savePng(path))
        return 4;
    std::puts("curveplot: export, clear, signed temperatures, tick rollover and gaps exercised");
    return 0;
}
