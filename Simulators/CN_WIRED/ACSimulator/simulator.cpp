#include <iostream>

#include "cn_wired.h"
#include "simulator.hpp"

// Some genius decided to omit this from Qt headers... Well...
inline std::ostream& operator<<(std::ostream& s, const QString& str)
{
    return s << str.toStdString();
}

// A helper class for dumping data
class Dump {
 public:
    Dump(const uint8_t* buf, int len) :buf_(buf), len_(len) {}

 private:
    const uint8_t *buf_;
    int len_;

    friend std::ostream& operator<<(std::ostream& s, const Dump& data) {
        for (int i = 0; i < data.len_ - 1; i++) {
            s << +data.buf_[i] << ' ';
        }
        return s << +data.buf_[data.len_ - 1];
    }
};

// Temporary, to be set from UI
bool dump_all_packets = true;
bool dump_all_modes   = true;

// Buffer to read incoming serial data
char rx_buffer[50];
uint8_t rx_bytes = 0;

struct ACState
{
    uint8_t setpoint;
    bool    poweroff;
    uint8_t mode;
    uint8_t fan;
};

static std::ostream& operator<<(std::ostream& s, const ACState& state)
{
    s << "set point = " << +state.setpoint
      << " power " << (state.poweroff ? "OFF" : "ON")
      << " mode " << std::hex;
    // We want to print unknown enum values, if any, in hex, so just switch to hex
    // once above and forget
    switch (state.mode) {
    case CNW_DRY:
        s << "DRY";
        break;
    case CNW_FAN:
        s << "FAN";
        break;
    case CNW_COOL:
        s << "COOL";
        break;
    case CNW_HEAT:
        s << "HEAT";
        break;
    case CNW_AUTO:
        s << "AUTO";
        break;
    default:
        s << "UNKNOWN[" << +state.mode << ']';
        break;
    }
    s << " fan ";
    switch (state.fan) {
    case CNW_FAN_AUTO:
        s << "AUTO";
        break;
    case CNW_FAN_1:
        s << '1';
        break;
    case CNW_FAN_2:
        s << '2';
        break;
    case CNW_FAN_3:
        s << '3';
        break;
    case CNW_FAN_POWERFUL:
        s << "POWERFUL";
        break;
    case CNW_FAN_ECO:
        s << "ECO";
        break;
    default:
        s << "UNKNOWN[" << +state.fan << ']';
        break;
    }

    return s << std::dec;
}

static ACState current_state = {
    .setpoint = 23,
    .poweroff = true,
    .mode     = CNW_COOL,
    .fan      = CNW_FAN_1
};

QString openSerial(QSerialPort* serial, const QString& port)
{
    std::cout << "Opening serial port " << port << "..." << std::endl;
    serial->setPortName(port);
    serial->setBaudRate(QSerialPort::Baud115200);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);

    // Make sure our Rx buffer is clear
    rx_bytes = 0;

    if (!serial->open(QIODevice::ReadWrite)) {
        QString error = serial->errorString();

        std::cout << "Open failed: " << error << std::endl;
        return error;
    }

    std::cout << "Success!" << std::endl;
    return QString();
}

void closeSerial(QSerialPort* serial)
{
    std::cout << "Closing serial port" << std::endl;
    serial->close();
}

// We are receiving ASCII data from our brigde in the form:
// 0014727640 Rx1 80 23 00 23 12 01 00 10 F0 OK
// We are only interested in hexadecimal data itself, we'll do all the processing
// by ourselves. Note also that the first byte here is synthetic. It contains only
// start bit, which should always be equal to 1, hence 80. We ignore it for simplicity.
static void handleRxPacket()
{
    uint8_t binary_pkt[CNW_PKT_LEN];

    // "Tx" echo, ignore
    if (rx_buffer[0] == 'T' && rx_buffer[1] == 'x')
        return;

    const char *startp = strstr(rx_buffer, "Rx1 ");

    if (!startp || startp[6] != ' ') {
        std::cout << "Unexpected data received: " << rx_buffer << std::endl;
        return;
    }

    startp += 7;

    for (int i = 0; i < CNW_PKT_LEN; i++) {
        const char* ptr = &startp[i * 3];
        char* endp;

        binary_pkt[i] = strtoul(ptr, &endp, 16);
        if (endp != ptr + 2) {
            std::cout << "Invalid hex data at position " << i << ": " << rx_buffer << std::endl;
            return;
        }
    }

    if (dump_all_packets) {
        std::cout << "Rx: " << std::hex << Dump(binary_pkt, CNW_PKT_LEN) << std::dec << std::endl;
    }

    uint8_t crc = cnw_checksum(binary_pkt);

    if (binary_pkt[CNW_CRC_OFFSET] != crc) {
        std::cout << "Bad CRC: calculated " << std::hex << +crc
                  << " vs received " << +binary_pkt[CNW_CRC_OFFSET] << std::dec << std::endl;
        if (!dump_all_packets) {
            std::cout << "Packet data: " << std::hex << Dump(binary_pkt, CNW_PKT_LEN) << std::dec << std::endl;
        }
        return;
    }

    ACState new_state;

    new_state.setpoint = decode_bcd(binary_pkt[CNW_TEMP_OFFSET]);
    new_state.poweroff = binary_pkt[CNW_MODE_OFFSET] & CNW_MODE_POWEROFF;
    new_state.mode     = binary_pkt[CNW_MODE_OFFSET] & CNW_MODE_MASK;
    new_state.fan      = binary_pkt[CNW_FAN_OFFSET];

    std::cout << "Rx: " << new_state << std::endl;

    if (new_state.setpoint != current_state.setpoint) {
        current_state.setpoint = new_state.setpoint;
        ui_UpdateSetPoint(current_state.setpoint);
    }
    if (new_state.poweroff != current_state.poweroff) {
        current_state.poweroff = new_state.poweroff;
        ui_UpdatePower(!current_state.poweroff);
    }
    if (new_state.mode != current_state.mode) {
        current_state.mode = new_state.mode;
        ui_UpdateMode(current_state.mode);
    }
    if (new_state.fan != current_state.fan) {
        current_state.fan = new_state.fan;
        ui_UpdateFan(current_state.fan);
    }
}

void serialRead(QSerialPort* serial)
{
    while (serial->read(&rx_buffer[rx_bytes], 1)) {
        if (rx_buffer[rx_bytes] == '\r') {
            // CR is end of line, just like in a regular terminal
            rx_buffer[rx_bytes] = 0;
            handleRxPacket();
            rx_bytes = 0;
        }
        if (rx_buffer[rx_bytes] != '\n') {
            // Ignore LFs
            if (rx_bytes == sizeof(rx_buffer) - 1) {
                std::cout << "Garbage on serial port; buffer exceeded!" << std::endl;
            } else {
                rx_bytes++;
            }
        }
    }
}

static void sendPacket(uint8_t* pkt, QSerialPort* serial)
{
    // Two hext digits per byte plus \r plus \0
    char hex[CNW_PKT_LEN * 2 + 2];

    pkt[CNW_CRC_OFFSET] = cnw_checksum(pkt);

    if (dump_all_packets) {
        std::cout << "Tx " << std::hex << Dump(pkt, CNW_PKT_LEN) << std::dec << std::endl;
    }

    sprintf(hex, "%02X%02X%02X%02X%02X%02X%02X%02X\r",
            pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5], pkt[6], pkt[7]);

    // Do not send trailing \0
    serial->write(hex, CNW_PKT_LEN * 2 + 1);
}

// Callbacks for the UI
void setIndoorTemp(uint8_t temp, QSerialPort* serial)
{
    uint8_t response[CNW_PKT_LEN] = {0};

    if (!serial)
        return;

    std::cout << "Tx indoors temp = " << temp << std::endl;

    response[CNW_TEMP_OFFSET] = encode_bcd(temp);
    response[1] = 0x04; // Unknown, dumped from an A/C
    response[2] = 0x50;

    sendPacket(response, serial);
}

void sendStatePacket(QSerialPort* serial)
{
    uint8_t response[CNW_PKT_LEN];

    if (!serial)
        return;

    std::cout << "Tx " << current_state << std::endl;

    response[CNW_TEMP_OFFSET] = encode_bcd(current_state.setpoint);
    response[1] = response[CNW_TEMP_OFFSET];
    response[2] = 0;
    response[CNW_MODE_OFFSET] = current_state.mode | (current_state.poweroff ? CNW_MODE_POWEROFF : 0);
    response[CNW_FAN_OFFSET] = current_state.fan;
    response[5] = 0x0A; // Unknown, dumped from an A/C
    response[6] = 0x10;
    response[CNW_CRC_OFFSET] = CNW_MODE_CHANGED;

    sendPacket(response, serial);
}

void setPower(bool on, QSerialPort* serial)
{
    // These callbacks are invoked by UI signals; and UI signals
    // are also issued when we set a property after parsing a received packet.
    // These conditionals catch signals generated by ourselves and prevent
    // ping-pong effect.
    if (current_state.poweroff == !on)
        return;

    current_state.poweroff = !on;
    sendStatePacket(serial);
}

void setPoint(uint8_t temp, QSerialPort* serial)
{
    if (current_state.setpoint == temp)
        return;

    current_state.setpoint = temp;
    sendStatePacket(serial);
}

void setMode(uint8_t mode, QSerialPort* serial)
{
    if (current_state.mode == mode)
        return;

    current_state.mode = mode;
    sendStatePacket(serial);
}

void setFan(uint8_t fan, QSerialPort* serial)
{
    if (current_state.fan == fan)
        return;

    current_state.fan = fan;
    sendStatePacket(serial);
}
