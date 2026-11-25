#include "mainwindow.h"
#include "curveplot.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

using Protocol::pressure;
using Protocol::temperature;
using Protocol::unsignedField;

MainWindow::MainWindow(QIODevice *testLink)
    : transport(testLink ? testLink : &port), simulated(testLink!=nullptr)
{
    buildUi();
    refreshPorts();
    connect(transport,&QIODevice::readyRead,this,&MainWindow::readIncoming);
    connect(&port,&QSerialPort::errorOccurred,this,[this](QSerialPort::SerialPortError e) {
        if (online && (e==QSerialPort::ResourceError || e==QSerialPort::ReadError ||
                       e==QSerialPort::WriteError || e==QSerialPort::DeviceNotFoundError))
            disconnectLink(port.errorString());
    });
    connect(&timer,&QTimer::timeout,this,&MainWindow::tick);
    timer.start(500);
    if (simulated && transport->isOpen()) connected();
    updateControls();
}
void MainWindow::buildUi()
{
    setWindowTitle(tr("冷媒罐循环泄压试验"));
    resize(1200,780);
    auto *base=new QWidget(this);
    auto *page=new QVBoxLayout(base);
    page->setContentsMargins(14,12,14,10);
    setCentralWidget(base);
    auto *bar=new QHBoxLayout;
    ports=new QComboBox; ports->setEditable(true); ports->setMinimumContentsLength(10);
    refreshButton=new QPushButton(tr("刷新"));
    connectButton=new QPushButton(tr("连接"));
    connection=new QLabel(tr("未连接"));
    bar->addWidget(new QLabel(tr("串口"))); bar->addWidget(ports); bar->addWidget(refreshButton);
    bar->addWidget(connectButton); bar->addWidget(connection); bar->addStretch();
    recoverButton=new QPushButton(tr("接收遗留记录")); recoverButton->setVisible(false);
    startButton=new QPushButton(tr("开始试验")); startButton->setObjectName("startButton");
    manualButton=new QPushButton(tr("手动开启加热")); manualButton->setObjectName("manualHeatButton");
    stopButton=new QPushButton(tr("停止试验")); stopButton->setObjectName("stopButton");
    setStyleSheet(QStringLiteral(
        "QMainWindow{background:#f5f7f8;color:#233643}"
        "QGroupBox{background:white;border:1px solid #cbd6db;border-radius:5px;"
        "margin-top:9px;padding-top:8px}QGroupBox::title{subcontrol-origin:margin;left:9px;"
        "padding:0 4px;color:#23556a}QLineEdit{padding:2px 4px}"
        "QPushButton{padding:4px 10px}QPushButton#startButton:enabled{background:#17647b;"
        "color:white;border:1px solid #135469;border-radius:4px}"
        "QPushButton#stopButton:enabled{color:#932e26;border:1px solid #be796e;"
        "background:#fff7f4;border-radius:4px}QLabel#stateLabel{font-weight:bold;color:#174b62}"
        "QTableWidget{alternate-background-color:#f4f8f9;gridline-color:#dbe3e6}"));
    bar->addWidget(recoverButton); bar->addWidget(manualButton); bar->addWidget(startButton); bar->addWidget(stopButton);
    page->addLayout(bar);
    auto *body=new QHBoxLayout; page->addLayout(body,1);
    auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(270); scroll->setMaximumWidth(325); body->addWidget(scroll);
    form=new QWidget; auto *left=new QVBoxLayout(form); left->setContentsMargins(0,0,7,0);
    QFormLayout *rows=nullptr, *limits=nullptr;
    for (int i=0;i<int(fields.size());++i) {
        if (i==0 || i==3 || i==8) {
            auto *g=new QGroupBox(i==0?tr("压力与温度上限"):i==3?tr("泄压口 NTC0"):tr("时限与轮次"));
            rows=new QFormLayout(g); left->addWidget(g);
            if (i==0) limits=rows;
        }
        const auto &s=Protocol::settings[i];
        QString key="cfg"+QString::number(i);
        auto *f=new QLineEdit(prefs.value(key,QString::fromLatin1(s.initial)).toString());
        fields[i]=f; f->setObjectName(key); f->setMaximumWidth(112);
        QString range=QString("%1–%2").arg(s.low/double(s.scale)).arg(s.high/double(s.scale));
        f->setPlaceholderText(i==1?tr("用首个采样值"):range);
        f->setToolTip(range+" "+QString::fromUtf8(s.unit));
        (i==Protocol::MaxTemperatureField ? limits : rows)->addRow(QString::fromUtf8(s.label)+" ("+QString::fromUtf8(s.unit)+")",f);
    }
    heatingMode=new QComboBox;
    heatingMode->setObjectName("heatingMode");
    heatingMode->addItem(tr("分段 PWM 调温"), false);
    heatingMode->addItem(tr("满功率调温"), true);
    heatingMode->setCurrentIndex(heatingMode->findData(prefs.value("fullPower",false).toBool()));
    limits->insertRow(0,tr("调温方式"),heatingMode);
    auto *dest=new QGroupBox(tr("试验保存位置")); auto *dst=new QVBoxLayout(dest);
    saveDirectory=new QLineEdit(prefs.value("directory",QDir(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("CoolingRelief")).toString());
    saveDirectory->setObjectName("saveDirectory"); dst->addWidget(saveDirectory);
    auto *browse=new QPushButton(tr("选择目录…")); dst->addWidget(browse); left->addWidget(dest);
    connect(browse,&QPushButton::clicked,this,[this] {
        QString d=QFileDialog::getExistingDirectory(this,tr("选择试验保存位置"),saveDirectory->text());
        if (!d.isEmpty()) saveDirectory->setText(d);
    });
    left->addStretch(); scroll->setWidget(form);
    auto *right=new QVBoxLayout; body->addLayout(right,1);
    stateLabel=new QLabel(tr("等待设备")); stateLabel->setObjectName("stateLabel");
    values=new QLabel(tr("压力 — MPa      NTC0 — ℃      NTC1 — ℃"));
    auto *metrics=new QHBoxLayout; metrics->addWidget(stateLabel); metrics->addStretch();
    metrics->addWidget(values); right->addLayout(metrics);
    monitorButton=new QPushButton(tr("实时观察…"));
    monitorButton->setObjectName("monitorButton");
    monitorButton->setToolTip(tr("独立查看设备实时曲线，可在试验前后使用"));
    metrics->addWidget(monitorButton);
    connect(monitorButton,&QPushButton::clicked,this,&MainWindow::openMonitor);
    auto *split=new QSplitter(Qt::Vertical);
    plot=new CurvePlot; split->addWidget(plot);
    auto *events=new QWidget; auto *eventLayout=new QVBoxLayout(events);
    eventLayout->setContentsMargins(0,0,0,0);
    auto *actions=new QHBoxLayout; actions->addWidget(new QLabel(tr("试验事件 · NTC0 判定")));
    actions->addStretch(); exportButton=new QPushButton(tr("导出曲线 PNG…"));
    actions->addWidget(exportButton); eventLayout->addLayout(actions);
    table=new QTableWidget(0,8); table->setObjectName("eventTable");
    table->setAlternatingRowColors(true);
    table->setHorizontalHeaderLabels({tr("轮次"),tr("板端时间 (s)"),tr("事件"),tr("压力 (MPa)"),tr("NTC0 (℃)"),
                                     tr("峰值 (MPa)"),tr("NTC1 (℃)"),tr("发生阶段")});
    table->verticalHeader()->hide(); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    eventLayout->addWidget(table); split->addWidget(events); split->setStretchFactor(0,3);
    split->setStretchFactor(1,2); split->setSizes({420,230}); right->addWidget(split,1);
    info=new QLabel(tr("连接设备后可查看实时采样。")); info->setObjectName("messageLabel");
    info->setWordWrap(true); info->setTextFormat(Qt::PlainText); page->addWidget(info);
    connect(refreshButton,&QPushButton::clicked,this,&MainWindow::refreshPorts);
    connect(connectButton,&QPushButton::clicked,this,[this] {
        if (online) {
            if (run && archive.active() && stage!=Stage::Finished) requestStop();
            if (!simulated && port.bytesToWrite()) port.waitForBytesWritten(100);
            disconnectLink(tr("已断开串口"));
        } else openLink();
    });
    connect(startButton,&QPushButton::clicked,this,[this] { begin(false); });
    connect(manualButton,&QPushButton::clicked,this,[this] {
        if (stage==Stage::Idle || stage==Stage::Finished) begin(true);
        else if (manualRun) requestStop();
    });
    connect(stopButton,&QPushButton::clicked,this,&MainWindow::requestStop);
    connect(exportButton,&QPushButton::clicked,this,&MainWindow::exportPlot);
    connect(plot,&CurvePlot::selectionChanged,this,&MainWindow::updateControls);
    connect(recoverButton,&QPushButton::clicked,this,&MainWindow::recoverForeign);
}
void MainWindow::showMessage(const QString &m) { info->setText(m); }
void MainWindow::openMonitor()
{
    if (!online || !greeted) return;
    if (!monitorWindow) {
        monitorWindow=new QDialog(this);
        monitorWindow->setWindowFlag(Qt::WindowMaximizeButtonHint);
        monitorWindow->resize(880,480);
        auto *layout=new QVBoxLayout(monitorWindow);
        monitorPlot=new CurvePlot(monitorWindow);
        layout->addWidget(monitorPlot,1);
        saveObservation=new QPushButton(tr("保存观察曲线 PNG…"),monitorWindow);
        layout->addWidget(saveObservation,0,Qt::AlignRight);
        connect(monitorPlot,&CurvePlot::selectionChanged,this,[this] {
            saveObservation->setEnabled(monitorPlot->canExportSelection());
        });
        connect(saveObservation,&QPushButton::clicked,this,[this] {
            auto *file=new QFileDialog(monitorWindow,tr("保存本次观察曲线"),
                QDir(saveDirectory->text()).filePath("observation.png"),tr("PNG 图像 (*.png)"));
            file->setAttribute(Qt::WA_DeleteOnClose);
            file->setAcceptMode(QFileDialog::AcceptSave);
            file->setDefaultSuffix("png");
            connect(file,&QFileDialog::fileSelected,this,[this](const QString &path) {
                showMessage(monitorPlot->savePng(path) ? tr("观察曲线已保存到 ")+path : tr("无法保存观察曲线到 ")+path);
            });
            file->open();
        });
    }
    if (!monitorWindow->isVisible()) {
        monitorPlot->clear();
        monitorLastMs.reset();
        saveObservation->setEnabled(false);
        monitorWindow->setWindowTitle(tr("实时观察"));
        monitorWindow->show();
    } else if (monitorWindow->isMinimized()) {
        monitorWindow->showNormal();
    }
    monitorWindow->raise();
    monitorWindow->activateWindow();
}
void MainWindow::updateControls()
{
    bool idle=stage==Stage::Idle || stage==Stage::Finished;
    connectButton->setText(online?tr("断开"):tr("连接"));
    ports->setEnabled(!online); refreshButton->setEnabled(!online);
    form->setEnabled(idle || stage==Stage::Foreign);
    bool manualActive=manualRun && (stage==Stage::Configure || stage==Stage::Start || stage==Stage::Running);
    manualButton->setText(manualRun && stage==Stage::Stopping ? tr("正在关闭加热") :
                          manualActive ? tr("关闭手动加热") : tr("手动开启加热"));
    manualButton->setEnabled(online && greeted && (idle || manualActive));
    startButton->setEnabled(online && greeted && idle);
    monitorButton->setEnabled(online && greeted);
    stopButton->setEnabled(online && greeted && !idle);
    recoverButton->setVisible(online && greeted && stage==Stage::Foreign);
    exportButton->setEnabled(stage==Stage::Finished && !archive.directory.isEmpty() && plot->canExportSelection());
}
void MainWindow::refreshPorts()
{
    QString current=ports->currentText();
    if (current.isEmpty()) current=prefs.value("port").toString();
    ports->clear();
    for (const auto &p : QSerialPortInfo::availablePorts()) ports->addItem(p.portName());
    if (!current.isEmpty()) ports->setCurrentText(current);
}
void MainWindow::openLink()
{
    if (!simulated) {
        port.setPortName(ports->currentText().trimmed()); port.setBaudRate(115200);
        port.setDataBits(QSerialPort::Data8); port.setParity(QSerialPort::NoParity);
        port.setStopBits(QSerialPort::OneStop); port.setFlowControl(QSerialPort::NoFlowControl);
        if (!port.open(QIODevice::ReadWrite)) { showMessage(port.errorString()); return; }
        prefs.setValue("port",port.portName());
    }
    connected();
}
void MainWindow::connected()
{
    temperatureHoldRun=0;
    online=true; greeted=false; retries=0; buffer.clear(); replyAge.start(); phaseAge.start();
    connection->setText(tr("握手中")); send("rc hello"); updateControls();
}
void MainWindow::disconnectLink(const QString &reason)
{
    online=false; greeted=false; buffer.clear();
    if (!simulated && port.isOpen()) port.close();
    if (archive.active() && stage!=Stage::Finished) {
        archive.note(reason); stage=Stage::Interrupted;
    }
    connection->setText(tr("未连接"));
    if (monitorWindow) monitorWindow->setWindowTitle(tr("实时观察 · 已断开"));
    showMessage(reason+(stage==Stage::Interrupted ? tr("；试验通信中断，重新连接后可收取剩余事件。") : QString()));
    updateControls();
}
bool MainWindow::send(const QByteArray &command)
{
    if (!online) return false;
    QByteArray line=command+'\r';
    if (transport->write(line)!=line.size()) { disconnectLink(tr("串口发送失败")); return false; }
    return true;
}
void MainWindow::readIncoming()
{
    buffer+=transport->readAll();
    qsizetype end;
    while ((end=buffer.indexOf('\n'))>=0) {
        QByteArray line=buffer.left(end).trimmed(); buffer.remove(0,end+1);
        // FinSH can prepend a prompt to the next asynchronous message.
        int start=line.lastIndexOf("@R,");
        if (start>=0 && line.size()-start<240) process(line.mid(start).split(','));
    }
    if (buffer.size()>2048) buffer.clear();
}

bool MainWindow::config(bool manual, QByteArray &cfg, QString &error) const
{
    QList<QByteArray> encoded;
    for (int i=0;i<Protocol::SettingCount;++i) {
        if (manual && i!=0 && i!=Protocol::MaxTemperatureField) continue;
        const auto &s=Protocol::settings[i];
        QString input=fields[i]->text().trimmed();
        int digits=s.scale==1000?3:s.scale==10?1:0;
        int upper=i==0 || i==1 ? hardLimit-1 : i==Protocol::MaxTemperatureField ? hardTemperature : s.high;
        QString pattern=digits ? QStringLiteral("^[0-9]+(?:\\.[0-9]{1,%1})?$").arg(digits)
                               : QStringLiteral("^[0-9]+$");
        int raw;
        if (i==1 && input.isEmpty()) raw=-1;
        else {
            bool ok=false; double v=input.toDouble(&ok);
            if (!ok || !QRegularExpression(pattern).match(input).hasMatch() ||
                v<s.low/double(s.scale) || v>upper/double(s.scale)) {
                error=tr("%1请输入 %2 至 %3 %4，最多 %5 位小数。")
                    .arg(QString::fromUtf8(s.label)).arg(s.low/double(s.scale)).arg(upper/double(s.scale))
                    .arg(QString::fromUtf8(s.unit)).arg(digits); return false;
            }
            raw=qRound(v*s.scale);
        }
        encoded<<QByteArray::number(raw);
    }
    // Omitting the optional field preserves the PWM command accepted by older boards.
    if (heatingMode->currentData().toBool()) encoded << "1";
    cfg=encoded.join(','); return true;
}
void MainWindow::sendConfig()
{
    const QByteArray command=manualRun ? "rc mcfg " : "rc cfg ";
    send(command+QByteArray::number(run)+" "+pendingConfig);
}
void MainWindow::begin(bool manual)
{
    if (!online || !greeted || !(stage==Stage::Idle || stage==Stage::Finished)) return;
    QByteArray cfg; QString err;
    if (!config(manual,cfg,err)) { showMessage(err); return; }
    if (saveDirectory->text().trimmed().isEmpty()) { showMessage(tr("请选择试验保存目录。")); return; }
    if (archive.active() && run==boardRun) heartbeat();
    if (!online) return;
    quint32 nextRun;
    do { nextRun=QRandomGenerator::global()->bounded(1U,2147483648U); } while (nextRun==boardRun);
    if (!archive.create(saveDirectory->text().trimmed(),nextRun,cfg)) { showMessage(archive.error); return; }
    manualRun=manual; run=nextRun; pendingConfig=cfg; stage=Stage::Configure; retries=0; phaseAge.restart();
    outcome.clear(); diskFailed=false; awaitingStop=false; recordedSampleAge.invalidate();
    plot->clear(); lastSequence.reset(); table->setRowCount(0);
    prefs.setValue("archives/"+QString::number(run),archive.directory);
    prefs.setValue("pendingPort",simulated ? QStringLiteral("test") : port.portName());
    prefs.setValue("pendingRun",run);
    prefs.setValue("directory",saveDirectory->text().trimmed());
    prefs.sync();
    stateLabel->setText(tr("提交参数")); showMessage(tr("正在核对板端接受的参数。"));
    updateControls(); sendConfig();
}
void MainWindow::requestStop()
{
    quint32 id=stage==Stage::Foreign ? boardRun : run;
    if (!online || !id) return;
    awaitingStop=true;
    if (stage!=Stage::Foreign && stage!=Stage::Recover) {
        stage=Stage::Stopping; phaseAge.restart(); retries=0;
    }
    showMessage(tr("已请求停热，等待设备确认。"));
    send("rc stop "+QByteArray::number(id)); updateControls();
}
void MainWindow::fault(const QString &reason)
{
    if (archive.active()) archive.note(reason);
    if (prefs.value("pendingRun").toUInt()==run) prefs.remove("pendingRun");
    outcome=reason; stage=Stage::Finished;
    stateLabel->setText(tr("试验中断")); showMessage(reason);
    updateControls();
}
void MainWindow::archiveFailure()
{
    if (diskFailed) return;
    diskFailed=true; awaitingStop=true;
    QString err=archive.error;
    if (online && run) send("rc stop "+QByteArray::number(run));
    archive.close(); stage=Stage::Interrupted;
    showMessage(tr("记录写入失败，已请求停热。请检查磁盘后重新连接。 ")+err);
    updateControls();
}
void MainWindow::checkSampleDeadline()
{
    constexpr qint64 sampleTimeoutMs=1500;
    if ((stage!=Stage::Start && stage!=Stage::Running) || !recordedSampleAge.isValid() ||
        recordedSampleAge.elapsed()<sampleTimeoutMs) return;
    requestStop();
    const QString reason=tr("超过 1.5 秒未记录到新的采样，已请求停热。");
    showMessage(reason);
    if (archive.active() && !archive.note(reason)) archiveFailure();
}
void MainWindow::heartbeat()
{
    checkSampleDeadline();
    // Every ping, including an event acknowledgement, renews the heating lease.
    if (!online || !greeted || diskFailed || awaitingStop ||
        stage==Stage::Foreign || stage==Stage::Recover || stage==Stage::Interrupted) return;
    quint32 ack=archive.active() && boardRun==run ? archive.ack : 0;
    send("rc ping "+QByteArray::number(boardRun)+" "+QByteArray::number(ack));
}
void MainWindow::tick()
{
    if (!online) return;
    if (!greeted) {
        if (phaseAge.elapsed()>=1000) {
            if (++retries>4) { disconnectLink(tr("握手超时，请核对串口和试验固件")); return; }
            phaseAge.restart(); send("rc hello");
        }
        return;
    }
    checkSampleDeadline();
    if (replyAge.elapsed()>3500) { disconnectLink(tr("设备通信超时")); return; }
    const bool observingForeign=stage==Stage::Foreign && monitorWindow && monitorWindow->isVisible();
    // hello keeps telemetry streaming without acknowledging events or renewing a foreign heating lease.
    if (replyAge.elapsed()>1400 || (observingForeign && monitorHelloAge.elapsed()>=1500)) {
        monitorHelloAge.restart();
        send("rc hello");
    }
    if (stage==Stage::Foreign || diskFailed) return;
    QByteArray id=QByteArray::number(run);
    if (stage==Stage::Recover) {
        if (phaseAge.elapsed()>=1000) {
            if (++retries>4) { disconnectLink(tr("无法读取设备事件确认序号")); return; }
            phaseAge.restart(); send("rc sync "+id);
        }
        return;
    }
    if ((stage==Stage::Configure || stage==Stage::Start || stage==Stage::Stopping) && phaseAge.elapsed()>=1000) {
        phaseAge.restart();
        if (++retries>3) {
            if (stage!=Stage::Configure) send("rc stop "+id);
            disconnectLink(tr("命令应答超时")); return;
        }
        if (stage==Stage::Configure) sendConfig();
        else if (stage==Stage::Start) send("rc start "+id);
        else send("rc stop "+id);
    }
    heartbeat();
}
void MainWindow::process(const QList<QByteArray> &f)
{
    if (!online || f.size()<3) return;
    QByteArray type=f[1];
    if (type=="H") {
        if (f[2]!="2") { disconnectLink(tr("请使用支持最高温度和手动加热的配套工装固件（协议 2）")); return; }
        quint32 limit,temperatureLimit,remote;
        if (f.size()!=7 || !unsignedField(f[3],limit) || !unsignedField(f[4],temperatureLimit) ||
            !unsignedField(f[5],remote) || Protocol::stateName(QString::fromLatin1(f[6])).isEmpty()) return;
        if (limit<2 || limit>1600 || !temperatureLimit || temperatureLimit>32767) {
            disconnectLink(tr("板端温压限值无效")); return;
        }
        hardLimit=int(limit); hardTemperature=int(temperatureLimit); replyAge.restart();
        handshake(remote,QString::fromLatin1(f[6])); return;
    }
    if (!greeted) return;
    quint32 id;
    if (!unsignedField(f[2],id)) return;
    if (type=="T" && f.size()==5 && id && id==boardRun && f[4]=="HOLD") {
        quint32 sequence;
        if (unsignedField(f[3],sequence)) {
            temperatureHoldRun=id;
            temperatureHoldSequence=sequence;
        }
        return;
    }
    if (type=="S" || type=="E") {
        bool event=type=="E";
        auto p=Protocol::point(f,event);
        if (!p) return;
        if (id!=boardRun) {
            if (id==0 && boardRun) send("rc hello");
            return;
        }
        replyAge.restart();
        if (event) {
            acceptEvent(*p);
        } else {
            if (temperatureHoldRun && id==temperatureHoldRun && p->number==temperatureHoldSequence && p->code=="HEAT")
                p->code=QStringLiteral("TEMP_HOLD");
            temperatureHoldRun=0;
            acceptSample(*p);
        }
        return;
    }
    if (id!=run) return;
    if (type=="Y" && f.size()==5 && stage==Stage::Recover) {
        quint32 acknowledged,last;
        if (!unsignedField(f[3],acknowledged) || !unsignedField(f[4],last) || acknowledged>last) return;
        if (!archive.note(tr("设备已有 %1 条事件得到确认，恢复剩余 %2 条。").arg(acknowledged).arg(last-acknowledged))) {
            archiveFailure(); return;
        }
        archive.ack=std::max(archive.ack,acknowledged);
        stage=archive.ack==last ? Stage::Finished : Stage::Stopping;
        if (stage==Stage::Finished) {
            outcome=tr("遗留记录已接收"); stateLabel->setText(outcome);
            showMessage(outcome+" · "+archive.directory);
            if (prefs.value("pendingRun").toUInt()==run) prefs.remove("pendingRun");
        }
        phaseAge.restart(); retries=0; replyAge.restart(); updateControls(); return;
    }
    if (type=="C" && f.size()>=4 &&
        (stage==Stage::Configure || stage==Stage::Stopping)) {
        QByteArray echoed=f.mid(3).join(',');
        if (stage==Stage::Stopping) {
            // OFF can be pressed before the board echoes the configuration.
            boardRun=run;
            send("rc stop "+QByteArray::number(run));
            return;
        }
        if (echoed!=pendingConfig) { fault(tr("设备回显参数不一致，已取消启动。")); return; }
        boardRun=run;
        if (!archive.note(tr("下位机已确认参数 %1").arg(QString::fromLatin1(echoed)))) {
            archiveFailure(); return;
        }
        for (int i=0;i<Protocol::SettingCount;++i)
            if (!manualRun || i==0 || i==Protocol::MaxTemperatureField)
                prefs.setValue("cfg"+QString::number(i),fields[i]->text().trimmed());
        prefs.setValue("fullPower",heatingMode->currentData().toBool());
        prefs.sync();
        stage=Stage::Start; phaseAge.restart(); retries=0; replyAge.restart(); recordedSampleAge.start();
        stateLabel->setText(tr("等待启动应答")); send("rc start "+QByteArray::number(run)); return;
    }
    if (type=="A" && f.size()==4) {
        if (f[3]=="START" && stage==Stage::Start) {
            stage=Stage::Running; stateLabel->setText(tr("已启动"));
            showMessage(tr("试验记录保存到 ")+archive.directory); replyAge.restart();
        } else if (f[3]=="STOP" && awaitingStop) {
            // STOP is a command acknowledgement; terminal records still need to be saved.
            awaitingStop=false; phaseAge.restart(); showMessage(tr("设备已接受停止命令，正在收取结束记录。"));
        }
        updateControls();
    } else if (type=="X" && f.size()==4) {
        QString reason=tr("设备拒绝命令（%1）").arg(QString::fromLatin1(f[3]));
        if (stage==Stage::Configure && f[3]=="FORMAT" && heatingMode->currentData().toBool())
            reason=tr("设备未接受满功率参数，请确认下位机固件支持满功率调温。");
        if (stage==Stage::Configure) { fault(reason); send("rc hello"); }
        else if (stage==Stage::Start) { requestStop(); showMessage(reason+tr("，已请求停热。")); }
        else showMessage(reason);
    }
}
void MainWindow::handshake(quint32 remote, const QString &state)
{
    bool reconnect=!greeted;
    const bool sameForeign=!reconnect && stage==Stage::Foreign && remote==boardRun;
    monitorHelloAge.start();
    if (reconnect && monitorWindow && monitorWindow->isVisible()) {
        monitorPlot->clear();
        monitorLastMs.reset();
        saveObservation->setEnabled(false);
        monitorWindow->setWindowTitle(tr("实时观察"));
    }
    greeted=true; boardRun=remote; boardState=state;
    if (remote==run && (state=="ABORT" || state=="DONE")) awaitingStop=false;
    connection->setText(tr("已连接 · 硬截止 %1 MPa / %2 ℃").arg(pressure(hardLimit),temperature(hardTemperature)));
    const QString range=tr("0.001–%1").arg(pressure(hardLimit-1));
    fields[Protocol::MaxTemperatureField]->setToolTip(
        tr("自动循环接近设定温度时暂停加热，回落后补热；手动加热达到设定值时结束。可设 0.1–%1 ℃").arg(temperature(hardTemperature)));
    fields[0]->setPlaceholderText(range); fields[0]->setToolTip(range+" MPa");
    fields[1]->setToolTip(tr("仅用于泄压后回压。留空取首个压力采样，可设 0.000–%1 MPa").arg(pressure(hardLimit-1)));
    if (sameForeign || (!reconnect && run==remote && stage!=Stage::Interrupted)) return;
    if (archive.active() && remote!=run && stage!=Stage::Finished && stage!=Stage::Configure) {
        fault(tr("设备已复位或试验编号改变，旧记录已标为中断。"));
    }
    if (!remote && !archive.active() && stage==Stage::Idle) {
        quint32 pending=prefs.value("pendingRun").toUInt();
        QString key="archives/"+QString::number(pending);
        QString link=simulated ? QStringLiteral("test") : port.portName();
        if (pending && prefs.value("pendingPort").toString()==link &&
            archive.reopen(prefs.value(key).toString(),pending)) {
            run=pending; restoreView();
            if (archive.sawTerminal) { stage=Stage::Finished; prefs.remove("pendingRun"); }
            else fault(tr("设备当前空闲；上一次试验的通信中断，板端结束事件不可用。"));
        }
    }
    if (!remote) {
        if (stage==Stage::Interrupted || (reconnect && stage==Stage::Configure))
            fault(tr("设备当前空闲，上一次试验已中断。"));
        if (stage==Stage::Foreign) stage=Stage::Idle;
        if (stage==Stage::Idle) {
            stateLabel->setText(tr("空闲")); showMessage(tr("填写试验参数后可开始。"));
        }
    } else {
        if (remote!=run || !archive.active()) {
            const QString path=prefs.value("archives/"+QString::number(remote)).toString();
            if (!path.isEmpty() && archive.reopen(path,remote)) {
                run=remote; diskFailed=false;
                if (!archive.sawTerminal) {
                    awaitingStop=true; send("rc stop "+QByteArray::number(run));
                }
                restoreView();
                stage=archive.sawTerminal ? Stage::Finished : Stage::Interrupted;
            } else {
                stage=Stage::Foreign;
                showMessage(tr("设备保留试验 %1 的记录。可接收遗留记录后开始新试验。").arg(remote));
            }
        }
        if (stage==Stage::Interrupted) {
            if (archive.partial) {
                stage=Stage::Recover; retries=0; phaseAge.restart();
                awaitingStop=true;
                if (!send("rc stop "+QByteArray::number(remote))) return;
                send("rc sync "+QByteArray::number(remote));
            } else if (state=="DONE" || state=="ABORT") {
                stage=Stage::Stopping; retries=0; phaseAge.restart();
                showMessage(tr("通信已恢复，正在补收结束记录。"));
            } else requestStop();
        }
    }
    updateControls();
}
void MainWindow::recoverForeign()
{
    if (stage!=Stage::Foreign || !boardRun) return;
    if (!archive.create(saveDirectory->text().trimmed(),boardRun,{})) { showMessage(archive.error); return; }
    run=boardRun; diskFailed=false; outcome.clear(); plot->clear();
    table->setRowCount(0); lastSequence.reset();
    prefs.setValue("archives/"+QString::number(run),archive.directory);
    prefs.setValue("pendingPort",simulated ? QStringLiteral("test") : port.portName());
    prefs.setValue("pendingRun",run); prefs.sync();
    if (!archive.note(tr("接收遗留试验的可用尾段；先前采样、已确认事件和原试验参数可能缺失。"))) {
        archiveFailure(); return;
    }
    stage=Stage::Recover; retries=0; phaseAge.restart(); awaitingStop=true;
    showMessage(tr("已请求停热，正在核对设备保留的事件。"));
    if (!send("rc stop "+QByteArray::number(run))) return;
    send("rc sync "+QByteArray::number(run));
    updateControls();
}
void MainWindow::acceptSample(const Protocol::Point &p)
{
    checkSampleDeadline();
    boardState=p.code;
    if (p.run==run && !p.heat && (p.code=="ABORT" || p.code=="DONE")) awaitingStop=false;
    QString status=Protocol::stateName(p.code);
    if (p.round>0) status=tr("第 %1 轮 · %2").arg(p.round).arg(status);
    if (stage!=Stage::Finished || outcome.isEmpty()) stateLabel->setText(status);
    values->setText(tr("压力 %1 MPa     NTC0 %2 ℃     NTC1 %3 ℃     加热%4")
        .arg(p.valid & 4 ? pressure(p.p) : QStringLiteral("—"))
        .arg(p.valid & 1 ? Protocol::outletTemperature(p.t0) : QStringLiteral("—"))
        .arg(p.valid & 2 ? temperature(p.t1) : QStringLiteral("—"))
        .arg(p.heat ? tr("开") : tr("关")));
    if (monitorWindow && monitorWindow->isVisible()) {
        // Sample frames arrive in wire order. A backwards board clock here means a new timeline.
        if (monitorLastMs && qint32(p.ms-*monitorLastMs)<0) monitorPlot->clear();
        monitorLastMs=p.ms;
        monitorPlot->addSample(p.number,p.ms,p.p,p.t0,p.t1,p.valid);
        saveObservation->setEnabled(monitorPlot->canExportSelection());
    }
    const bool preview=stage==Stage::Idle;
    if (!preview && (p.run!=run || !archive.active() || diskFailed ||
        stage==Stage::Finished || stage==Stage::Foreign || stage==Stage::Configure)) return;
    if (lastSequence && (p.number==*lastSequence || quint32(p.number-*lastSequence)>0x7fffffffU)) return;
    if (!preview && !archive.sample(p)) { archiveFailure(); return; }
    if (!preview && (stage==Stage::Start || stage==Stage::Running)) recordedSampleAge.restart();
    plot->addSample(p.number,p.ms,p.p,p.t0,p.t1,p.valid);
    lastSequence=p.number; updateControls();
}
void MainWindow::acceptEvent(const Protocol::Point &p)
{
    if (p.run!=run || stage==Stage::Recover || diskFailed || !archive.active() || p.number<=archive.ack) return;
    if (p.number!=archive.ack+1) {
        showMessage(tr("等待设备补发事件 %1。").arg(archive.ack+1)); return;
    }
    if (!archive.event(p)) { archiveFailure(); return; }
    appendEvent(p);
    if (Protocol::terminalEvent(p.code)) {
        awaitingStop=false;
        if (prefs.value("pendingRun").toUInt()==run) prefs.remove("pendingRun");
        stage=Stage::Finished; outcome=Protocol::eventName(p.code); stateLabel->setText(outcome);
        QString m=outcome+tr("。记录目录 ")+archive.directory;
        if (lastSequence && !plot->savePng(QDir(archive.directory).filePath("curve.png"),true))
            m+=tr("；曲线 PNG 保存失败，请重新导出。");
        showMessage(m); updateControls();
    }
    heartbeat();
}
void MainWindow::appendEvent(const Protocol::Point &p)
{
    QString name=Protocol::eventName(p.code);
    plot->addEvent(p.ms,name);
    QStringList row{QString::number(p.round),QString::number(p.ms/1000.,'f',3),name,
                    p.valid & 4 ? pressure(p.p) : QStringLiteral("—"),
                    p.valid & 1 ? Protocol::outletTemperature(p.t0) : QStringLiteral("—"),
                    p.peak || (p.valid & 4) ? pressure(p.peak) : QStringLiteral("—"),
                    p.valid & 2 ? temperature(p.t1) : QStringLiteral("—"),
                    p.stage.isEmpty() ? QStringLiteral("—") : Protocol::stateName(p.stage)};
    int n=table->rowCount();
    table->insertRow(n);
    for (int i=0;i<row.size();++i) table->setItem(n,i,new QTableWidgetItem(row[i]));
    table->scrollToBottom();
}
void MainWindow::restoreView()
{
    plot->clear(); lastSequence.reset(); table->setRowCount(0);
    QFile samples(QDir(archive.directory).filePath("samples.csv"));
    if (samples.open(QIODevice::ReadOnly)) {
        samples.readLine();
        while (!samples.atEnd()) {
            auto f=samples.readLine().trimmed().split(',');
            if (f.size()!=10 || f[0].toUInt()!=run) continue;
            lastSequence=f[1].toUInt();
            plot->addSample(*lastSequence,f[2].toUInt(),qRound(f[5].toDouble()*1000),
                 qRound(f[6].toDouble()*10),qRound(f[7].toDouble()*10),f[8].toInt());
        }
    }
    QFile events(QDir(archive.directory).filePath("events.csv"));
    if (events.open(QIODevice::ReadOnly)) {
        events.readLine();
        while (!events.atEnd()) {
            auto f=events.readLine().trimmed().split(',');
            if ((f.size()!=11 && f.size()!=13) || f[0].toUInt()!=run) continue;
            Protocol::Point p{}; p.number=f[1].toUInt(); p.ms=f[2].toUInt();
            p.round=f[3].toInt(); p.code=QString::fromLatin1(f[4]); p.p=qRound(f[5].toDouble()*1000);
            p.t0=qRound(f[6].toDouble()*10); p.t1=qRound(f[7].toDouble()*10);
            p.peak=qRound(f[8].toDouble()*1000);
            if (f.size()==13 && !f[12].isEmpty()) {
                p.stage=QString::fromLatin1(f[11]); p.valid=f[12].toInt();
            } else if (!Protocol::terminalEvent(p.code) || p.code=="DONE") {
                // These older control events were emitted only after all three channels passed validation.
                p.valid=(!f[5].isEmpty()?4:0)|(!f[6].isEmpty()?1:0)|(!f[7].isEmpty()?2:0);
            }
            appendEvent(p);
            if (Protocol::terminalEvent(p.code)) { outcome=Protocol::eventName(p.code); stateLabel->setText(outcome); }
        }
    }
}
void MainWindow::exportPlot()
{
    QString target=QFileDialog::getSaveFileName(this,tr("导出整段曲线"),QDir(archive.directory).filePath("curve.png"),
                                                tr("PNG 图像 (*.png)"));
    if (!target.isEmpty()) showMessage(plot->savePng(target) ? tr("曲线已保存到 ")+target : tr("无法写入 ")+target);
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (online && run && archive.active() && stage!=Stage::Finished) {
        requestStop(); if (!simulated && port.bytesToWrite()) port.waitForBytesWritten(150);
        archive.note(tr("上位机已退出；再次连接可接收板端结束记录。"));
    }
    QMainWindow::closeEvent(event);
}
