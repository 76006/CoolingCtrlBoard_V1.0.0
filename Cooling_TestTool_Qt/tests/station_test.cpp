#include "../mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QIODevice>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
class BoardLink final : public QIODevice
{
public:
    BoardLink() { open(QIODevice::ReadWrite | QIODevice::Unbuffered); }
    QByteArray sent;
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return incoming.size() + QIODevice::bytesAvailable(); }
    void feed(const QByteArray &bytes) { incoming += bytes; emit readyRead(); }
private:
    qint64 readData(char *data, qint64 size) override {
        const auto count = std::min<qint64>(size, incoming.size());
        std::memcpy(data, incoming.constData(), size_t(count));
        incoming.remove(0, count);
        return count;
    }
    qint64 writeData(const char *data, qint64 size) override {
        sent.append(data, size);
        return size;
    }
    QByteArray incoming;
};

void require(bool condition, const char *diagnosis)
{
    if (!condition) throw std::runtime_error(diagnosis);
}

void pump(int ms = 10)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    } while (timer.elapsed() < ms);
}

QByteArray fileContents(const QString &path)
{
    QFile input(path);
    require(input.open(QIODevice::ReadOnly), "cannot reopen saved CSV");
    return input.readAll();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory(QDir::current().filePath("wire-replay-XXXXXX"));
    if (!directory.isValid()) return 2;
    QApplication::setOrganizationName("ReliefStationTest");
    QApplication::setApplicationName("WireReplay");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    BoardLink board;
    MainWindow station(&board);
    try {
        station.setAttribute(Qt::WA_DontShowOnScreen);
        station.show();
        pump();
        require(board.sent.contains("rc hello"), "opening link did not request handshake");
        board.feed("boot ready\r\nmsh >@R,H,2,1400,700,0,IDLE\r\n");
        pump();
        const QStringList fields = {"1.000", "", "0.020", "1.0", "1.0", "0.3",
                                    "0.5", "0.2", "0.5", "5.0", "60.0", "2", "55.0"};
        for (int index = 0; index < fields.size(); ++index) {
            auto *field = station.findChild<QLineEdit *>(QString("cfg%1").arg(index));
            require(field, "missing test setting");
            field->setText(fields[index]);
        }
        auto *folder = station.findChild<QLineEdit *>("saveDirectory");
        auto *start = station.findChild<QPushButton *>("startButton");
        require(folder && start, "missing save/start controls");
        auto *mode = station.findChild<QComboBox *>("heatingMode");
        require(mode && !mode->currentData().toBool(), "PWM must be the initial mode");
        mode->setCurrentIndex(mode->findData(true));
        folder->setText(directory.path());
        pump();
        require(start->isEnabled(), "start disabled after handshake and valid settings");
        start->click();
        pump();
        const QRegularExpression cfgPattern("rc cfg ([0-9]+) ([-,0-9]+)");
        const auto match = cfgPattern.match(QString::fromLatin1(board.sent));
        require(match.hasMatch(), "no configuration sent");
        const auto run = match.captured(1).toLatin1();
        const auto cfg = match.captured(2).toLatin1();
        require(cfg == "1000,-1,20,10,10,3,5,2,5,50,600,2,550,1", "parameter order, scaling or full-power mode mismatch");
        require(!board.sent.contains("rc start"), "start sent before board accepted config");
        board.feed("@R,C," + run + "," + cfg + "\r\n");
        pump();
        require(board.sent.contains("rc start " + run), "config ACK did not trigger start");
        board.feed("@R,A," + run + ",START\r\n");
        // Mix shell echoes, split records, negative temperature, skipped sample, and event retries.
        board.feed("msh >rc ping\r\n@R,S," + run + ",41,1000,1,HE");
        board.feed("AT,712,110,220,7,1\r\n@R,S," + run + ",43,1200,1,CHECK,710,-5,221,7,0\r\n");
        const QByteArray trigger = "@R,E," + run + ",1,1200,1,OPEN,710,-5,221,725,VENT,7\r\n";
        board.feed(trigger + trigger);
        board.feed("@R,E," + run + ",2,1700,1,CLOSE,690,12,222,726,RETURN,7\r\n");
        board.feed("@R,E," + run + ",3,2100,1,ROUND,620,13,222,727,RETURN,7\r\n");
        board.feed("@R,E," + run + ",4,2200,2,SENSOR,0,13,222,620,HEAT,3\r\n");
        pump(650);
        require(board.sent.contains("rc ping " + run + " 4"), "durable contiguous event ACK missing");
        QDirIterator files(directory.path(), {"*.csv"}, QDir::Files, QDirIterator::Subdirectories);
        bool samplesFound = false;
        bool eventsFound = false;
        while (files.hasNext()) {
            const QByteArray data = fileContents(files.next());
            if (data.contains("0.712") && data.contains("-0.5")) samplesFound = true;
            if (data.contains("OPEN")) {
                require(data.count("OPEN") == 1, "duplicate OPEN written to CSV");
                require(data.contains("0.710") && data.contains("0.727") && data.contains("SENSOR,,")
                        && data.contains("HEAT,3"),
                        "event CSV missing trigger pressure, round peak, or terminal event");
                eventsFound = true;
            }
        }
        require(samplesFound && eventsFound, "incomplete sample or event CSV");
        QDirIterator images(directory.path(), {"curve.png"}, QDir::Files, QDirIterator::Subdirectories);
        require(images.hasNext() && !QImage(images.next()).isNull(), "terminal event did not archive the PNG");
        if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--capture")
            require(station.grab().save(QString::fromLocal8Bit(argv[2])), "could not capture station");
        qInfo("station: wire handshake, configuration, split frames, event ACK/dedup and CSV passed");
    } catch (const std::exception &error) {
        const auto *message = station.findChild<QLabel *>("messageLabel");
        fprintf(stderr, "station test: %s. UI: %s. Device received: %s\n", error.what(),
                message ? message->text().toUtf8().constData() : "", board.sent.constData());
        return 1;
    }
    return 0;
}
