#include "cn_wired.h"
#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "simulator.hpp"

static MainWindow* g_main;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow), serial(new QSerialPort(this))
{
    ui->setupUi(this);
    connect(serial, &QSerialPort::readyRead, this, &MainWindow::readData);

    // For UI update callbacks
    g_main = this;

    // Set initial values from the UI
    setPower(ui->powerState->isChecked(), nullptr);
    setPoint(ui->setPoint->value(), nullptr);
    setMode(getMode(), nullptr);
    setFan(getFan(), nullptr);
    setVSwing(ui->vSwing->isChecked(), nullptr);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_commButton_clicked()
{
    if (comm_running) {
        closeSerial(serial);
        ui->commButton->setText("Connect");
        ui->portName->setDisabled(false);
        comm_running = false;
    } else {
        QString err = openSerial(serial, ui->portName->text());

        if (err.isEmpty()) {
            ui->portName->setDisabled(true);
            ui->commButton->setText("Disconnect");
            comm_running = true;
        } else {
            // ui->statusbar->
        }
    }
}

void MainWindow::readData()
{
    serialRead(serial);
}

// Keep these in sync with ComboBox items in UI definition!
static uint8_t modes[] = {
    CNW_COOL,
    CNW_HEAT,
    CNW_AUTO,
    CNW_DRY,
    CNW_FAN
};

#define MODES_NUM 5

static uint8_t fan_speeds[] = {
    CNW_FAN_ECO,
    CNW_FAN_AUTO,
    CNW_FAN_1,
    CNW_FAN_2,
    CNW_FAN_3,
    CNW_FAN_POWERFUL
};

#define FAN_SPEED_NUM 6

static int findIndex(uint8_t val, const uint8_t* table, int table_size)
{
    for (int i = 0; i < table_size; i++) {
        if (table[i] == val)
            return i;
    }
    return -1;
}

uint8_t MainWindow::getMode() const
{
    return modes[ui->modeSelector->currentIndex()];
}

uint8_t MainWindow::getFan() const
{
    return fan_speeds[ui->fanSelector->currentIndex()];
}

void MainWindow::updateSetPoint(uint8_t setpoint)
{
    ui->setPoint->setValue(setpoint);
}

void MainWindow::updatePower(bool on)
{
    ui->powerState->setChecked(on);
}

void MainWindow::updateMode(uint8_t mode)
{
    int i = findIndex(mode, modes, MODES_NUM);

    if (i != -1)
        ui->modeSelector->setCurrentIndex(i);
}

void MainWindow::updateFan(uint8_t fan)
{
    int i = findIndex(fan, fan_speeds, FAN_SPEED_NUM);

    if (i != -1)
        ui->fanSelector->setCurrentIndex(i);
}

void MainWindow::updateVSwing(bool on)
{
    ui->vSwing->setChecked(on);
}

// These are called from within simulator.cpp in order to update the UI state
void ui_UpdateSetPoint(uint8_t setpoint)
{
    g_main->updateSetPoint(setpoint);
}

void ui_UpdatePower(bool on)
{
    g_main->updatePower(on);
}

void ui_UpdateMode(uint8_t mode)
{
    g_main->updateMode(mode);
}

void ui_UpdateFan(uint8_t fan)
{
    g_main->updateFan(fan);
}

void ui_UpdateVSwing(bool on)
{
    g_main->updateVSwing(on);
}

void MainWindow::on_currentTemp_valueChanged(int arg1)
{
    setIndoorTemp(arg1, serial);
}

void MainWindow::on_powerState_stateChanged(int arg1)
{
    setPower(arg1 == Qt::Checked, serial);
}

void MainWindow::on_setPoint_valueChanged(int arg1)
{
    setPoint(arg1, serial);
}

void MainWindow::on_modeSelector_currentIndexChanged(int index)
{
    setMode(modes[index], serial);
}

void MainWindow::on_fanSelector_currentIndexChanged(int index)
{
    setFan(fan_speeds[index], serial);
}

void MainWindow::on_vSwing_stateChanged(int arg1)
{
    setVSwing(arg1 == Qt::Checked, serial);
}
