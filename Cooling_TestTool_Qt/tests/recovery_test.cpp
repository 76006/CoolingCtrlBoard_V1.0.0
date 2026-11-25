#include "../mainwindow.h"
#include <QApplication>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
class SerialReplay final : public QIODevice
{
public:
    SerialReplay() { open(QIODevice::ReadWrite | QIODevice::Unbuffered); }
    QByteArray commands;
    void receive(const QByteArray &line) {
        pending += line + "\r\n";
        emit readyRead();
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return pending.size() + QIODevice::bytesAvailable(); }
private:
    QByteArray pending;
    qint64 readData(char *data, qint64 size) override {
        const qint64 count = std::min<qint64>(pending.size(), size);
        std::memcpy(data, pending.constData(), count);
        pending.remove(0, count);
        return count;
    }
    qint64 writeData(const char *data, qint64 size) override {
        commands.append(data, size);
        return size;
    }
};

void require(bool result, const char *message)
{
    if (!result) throw std::runtime_error(message);
}
QPushButton *button(MainWindow &window, const QString &text)
{
    for (auto *b : window.findChildren<QPushButton *>()) if (b->text()==text) return b;
    throw std::runtime_error("expected operator control missing");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir storage(QDir::current().absoluteFilePath("recovery-XXXXXX"));
    if (!storage.isValid()) return 2;
    QApplication::setOrganizationName("ReliefStationTest");
    QApplication::setApplicationName("RecoverRun");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());
    try {
        SerialReplay link;
        MainWindow station(&link);
        link.receive("@R,H,2,1400,700,0,IDLE");
        const QStringList values={"1","","0.01","0.5","1","0.3","0.5","0.2","1","10","60","2"};
        for (int i=0;i<values.size();++i)
            station.findChild<QLineEdit *>("cfg"+QString::number(i))->setText(values[i]);
        auto *directory=station.findChild<QLineEdit *>("saveDirectory");
        auto *start=station.findChild<QPushButton *>("startButton");

        QFile blocked(storage.filePath("occupied"));
        require(blocked.open(QIODevice::WriteOnly), "cannot create invalid destination fixture");
        directory->setText(blocked.fileName());
        start->click();
        require(!link.commands.contains("rc cfg"), "configuration sent before creating archive");
        directory->setText(storage.path());
        start->click();
        auto match=QRegularExpression("rc cfg (\\d+) ([-,0-9]+)").match(QString::fromLatin1(link.commands));
        require(match.hasMatch(), "configuration missing");
        QByteArray run=match.captured(1).toLatin1();

        button(station,"断开")->click();
        button(station,"连接")->click();
        link.receive("@R,H,2,1400,700,"+run+",IDLE");
        require(link.commands.endsWith("rc stop "+run+"\r"), "orphan configuration not cancelled");
        link.receive("@R,A,"+run+",STOP");
        require(!start->isEnabled(), "STOP acknowledgement prematurely released next start");
        link.receive("@R,E,"+run+",1,400,1,STOP,100,100,100,120,BASE,7");
        require(start->isEnabled() && link.commands.endsWith("rc ping "+run+" 1\r"),
                "terminal event was not finalized and acknowledged");

        button(station,"断开")->click();
        button(station,"连接")->click();
        link.receive("@R,H,2,1400,700,777,ABORT");
        require(!start->isEnabled(), "unknown retained run ignored");
        button(station,"接收遗留记录")->click();
        require(link.commands.contains("rc sync 777"), "recovery never queried committed event cursor");
        const QByteArray last="@R,E,777,9,1200,2,LINK,0,0,100,500,HEAT,2";
        link.receive(last);
        require(!link.commands.contains("rc ping 777 9"), "event acknowledged before cursor known");
        link.receive("@R,Y,777,7,9");
        link.receive(last);
        require(!link.commands.contains("rc ping 777 9"), "missing predecessor skipped");
        link.receive("@R,E,777,8,1100,2,OPEN,400,100,100,500,CHECK,7");
        link.receive(last);
        require(start->isEnabled() && link.commands.contains("rc ping 777 9"),
                "retained events did not unblock new run");
        {
            SerialReplay reconnect;
            MainWindow restored(&reconnect);
            reconnect.receive("@R,H,2,1400,700,777,ABORT");
            auto *table=restored.findChild<QTableWidget *>("eventTable");
            require(table && table->rowCount()==2 && table->item(1,3)->text()=="—" &&
                    table->item(1,4)->text()=="—", "invalid fault channels restored as measured values");
        }
        link.commands.clear();
        start->click();
        require(!start->isEnabled(), "cannot start after recovery");
        match=QRegularExpression("rc cfg (\\d+) ([-,0-9]+)").match(QString::fromLatin1(link.commands));
        require(match.hasMatch(), "new configuration missing");
        run=match.captured(1).toLatin1();
        link.receive("@R,C,"+run+","+match.captured(2).toLatin1());
        link.receive("@R,A,"+run+",START");
        const QByteArray repeated="@R,S,"+run+",10,1000,1,HEAT,500,150,220,7,1";
        link.receive(repeated);
        QElapsedTimer wait; wait.start();
        while (wait.elapsed()<2400 && !link.commands.contains("rc stop "+run)) {
            link.receive("@R,H,2,1400,700,"+run+",HEAT");
            link.receive(repeated);
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        QByteArray stopCommand="rc stop "+run;
        require(link.commands.contains(stopCommand), "repeated sample and HELLO hid recording stall");
        auto stopAt=link.commands.indexOf(stopCommand);
        link.receive("@R,E,"+run+",1,1100,1,HEAT,500,150,220,500,HEAT,7");
        link.receive("@R,S,"+run+",11,1600,1,HEAT,502,151,221,7,1");
        wait.restart();
        while (wait.elapsed()<650) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        require(!link.commands.mid(stopAt).contains("rc ping "+run),
                "heartbeat extended heating before STOP confirmation");
        link.receive("@R,E,"+run+",2,1700,1,STOP,502,151,221,502,HEAT,7");
        require(start->isEnabled() && link.commands.endsWith("rc ping "+run+" 2\r"),
                "after stop, durable event confirmation did not resume");
        std::puts("archive, recovery cursor, validity, stalled sampling and STOP acknowledgement passed");
    } catch(const std::exception &error) {
        std::fprintf(stderr, "station recovery: %s\n", error.what());
        return 1;
    }
    return 0;
}
