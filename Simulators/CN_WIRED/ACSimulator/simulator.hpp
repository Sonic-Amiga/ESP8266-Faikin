#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <QSerialPort>

QString openSerial(QSerialPort* serial, const QString& port);
void closeSerial(QSerialPort* serial);
void serialRead(QSerialPort* serial);

void setIndoorTemp(int temp, QSerialPort* serial);
void setPower(bool on, QSerialPort* serial);
void setPoint(uint8_t temp, QSerialPort* serial);
void setMode(uint8_t mode, QSerialPort* serial);
void setFan(uint8_t fan, QSerialPort* serial);
void setVSwing(bool on, QSerialPort* serial);
void setDumpAllPackets(bool on);

void ui_UpdateSetPoint(uint8_t setpoint);
void ui_UpdatePower(bool on);
void ui_UpdateMode(uint8_t mode);
void ui_UpdateFan(uint8_t fan);
void ui_UpdateVSwing(bool on);

#endif // SIMULATOR_H
