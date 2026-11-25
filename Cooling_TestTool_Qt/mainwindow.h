#pragma once
#include "archive.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QSerialPort>
#include <QSettings>
#include <QTimer>
#include <array>

class CurvePlot;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QIODevice *testLink = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    enum class Stage { Idle, Configure, Start, Running, Stopping, Finished, Interrupted, Foreign, Recover };
    void buildUi();
    void updateControls();
    void refreshPorts();
    void openLink();
    void disconnectLink(const QString &reason);
    void connected();
    void tick();
    void checkSampleDeadline();
    void heartbeat();
    bool send(const QByteArray &command);
    void readIncoming();
    void process(const QList<QByteArray> &fields);
    void handshake(quint32 remote, const QString &state);
    void begin();
    void requestStop();
    void recoverForeign();
    void fault(const QString &reason);
    void archiveFailure();
    void acceptSample(const Protocol::Point &p);
    void acceptEvent(const Protocol::Point &p);
    void appendEvent(const Protocol::Point &p);
    void restoreView();
    void exportPlot();
    void showMessage(const QString &message);
    bool config(QByteArray &cfg, QString &error) const;
    QSerialPort port;
    QIODevice *transport;
    bool simulated, online=false, greeted=false, diskFailed=false, awaitingStop=false;
    Stage stage=Stage::Idle;
    quint32 run=0, boardRun=0;
    int hardLimit=1600, retries=0;
    std::optional<quint32> lastSequence;
    QString boardState, outcome;
    QByteArray buffer, pendingConfig;
    Archive archive;
    QSettings prefs;
    QTimer timer;
    QElapsedTimer replyAge, phaseAge, recordedSampleAge;
    QComboBox *ports=nullptr;
    QPushButton *connectButton=nullptr, *refreshButton=nullptr, *startButton=nullptr,
                *stopButton=nullptr, *exportButton=nullptr, *recoverButton=nullptr;
    QWidget *form=nullptr;
    QLineEdit *saveDirectory=nullptr;
    QLabel *connection=nullptr, *stateLabel=nullptr, *info=nullptr, *values=nullptr;
    CurvePlot *plot=nullptr;
    QTableWidget *table=nullptr;
    std::array<QLineEdit *,12> fields{};
};
