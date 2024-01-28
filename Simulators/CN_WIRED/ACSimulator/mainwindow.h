#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void updateSetPoint(uint8_t setpoint);
    void updatePower(bool on);
    void updateMode(uint8_t mode);
    void updateFan(uint8_t fan);
    void updateVSwing(bool on);

private slots:
    void on_commButton_clicked();
    void readData();

    void on_currentTemp_valueChanged(int arg1);

    void on_powerState_stateChanged(int arg1);

    void on_setPoint_valueChanged(int arg1);

    void on_modeSelector_currentIndexChanged(int index);

    void on_fanSelector_currentIndexChanged(int index);

    void on_vSwing_stateChanged(int arg1);

    void on_dumpAllPackets_stateChanged(int arg1);

    void on_portName_editingFinished();

private:
    void startSimulator();
    void setCommRunning(bool running);
    uint8_t getMode() const;
    uint8_t getFan() const;

    Ui::MainWindow *ui;

    QSerialPort* serial;
    bool comm_running = false;
};
#endif // MAINWINDOW_H
