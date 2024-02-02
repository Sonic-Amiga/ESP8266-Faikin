#include "terminal_in.hpp"

// Pins we're using. RX2 is optional, can be commented out if not used
#define RX1_PIN 2 // PD2
//#define RX2_PIN 3 // PD3
#define TX_PIN  4 // PD4

// Data packet length
#define PKT_LEN 8
// The packet contains a 4-bit checksum in its last byte
#define CRC_OFFSET PKT_LEN - 1
// Our RX buffers are prefixed by one byte, where start bit is stored.
// See start()
#define RX_LEN PKT_LEN + 1

// Pulse lengths in microseconds
#define SYNC_LENGTH  2600
#define START_LENGTH 1000
#define SPACE_LENGTH 300
#define BIT_1_LENGTH 900
#define BIT_0_LENGTH 400
#define END_DELAY    16000
#define END_LENGTH   2000
// Detection tolerance
#define THRESHOLD 200

class Receiver {
  public:
    Receiver(int pin) : pin(pin) {}

    void begin(void(*handler)()) {
      reset();
      ack = false;
      state = 1;
      pinMode(pin, INPUT);
      attachInterrupt(digitalPinToInterrupt(pin), handler, CHANGE);
    }

    void reset() {
      rx_bytes = -1; // This signals "Wait for sync" state
    }

    bool isBusy() const {
      // isReceiving state is only entered after SYNC pulse has ended.
      // Checking for !state covers the SYNC and the final space.
      return !state || isReceiving();
    }

    bool isReceiving() const {
      return rx_bytes >= 0 && rx_bytes < RX_LEN;
    }

    bool isDataReady() const {
      return rx_bytes == RX_LEN;
    }

    const uint8_t* getBuffer() const {
      return buffer;
    }

    // 0th byte contains start bit
    const uint8_t* getData() const {
      return &buffer[1];
    }

    unsigned long getRxTimestamp() const {
      return last_rx;
    }

    bool getAck(unsigned long &timestamp) {
      if (ack) {
        timestamp = last_ack;
        ack = false;
        return true;
      } else {
        return false;
      }
    }

    void onInterrupt();

  private:
    void start() {
      // The packet always starts with a single bit of 1, which is apparently
      // a start bit, so the 0th byte will contain this single bit. This also lets
      // us make sure that we're correct, and it's indeed always 1.
      // The next bit is going to be LSB of 1st byte.
      rx_bytes  = 0;
      rx_bits   = 7;
      buffer[0] = 0;
    }

    void receiveBit(uint8_t v) {
      buffer[rx_bytes] |= (v << rx_bits);

      if (rx_bits == 7) {
        rx_bits = 0;
        rx_bytes++;
        // Prepare the next byte. We don't check for overflow for speed, that's
        // why we need one spare trailing byte
        buffer[rx_bytes] = 0;
      } else {
        rx_bits++;
      }
    }

    uint8_t       buffer[RX_LEN + 1];
    unsigned long pulse_start; // Pulse start time
    unsigned int  state;       // Current line state
    int           rx_bytes;    // Bytes counter
    int           rx_bits;     // Bits counter
    volatile bool ack;
    unsigned long last_rx;     // Timestamp of the received packet
    unsigned long last_ack;    // Timestamp of the last ACK pulse
    const int     pin;
};

void Receiver::onInterrupt() {
  int new_state = digitalRead(pin);

  if (new_state == state) {
    return; // Some rubbish
  }

  unsigned long now = micros();

  if (new_state) {
    // LOW->HIGH, start of data bit
    unsigned long length = now - pulse_start;

    if (length > SYNC_LENGTH - THRESHOLD) {
      // Got SYNC pulse, start receiving data
      last_rx = pulse_start;
      start();
    } else if (length > END_LENGTH - THRESHOLD) {
      // Got ACK pulse
      if (!ack) {
          last_ack = pulse_start;
          ack = true;
      }
    }
  } else {
    // HIGH->LOW, start of SYNC or end of data bit
    if (isReceiving()) {
      // High bit - 900us, low bit - 400us
      uint8_t bit = (now - pulse_start) > 700;
      receiveBit(bit);
    }
  }

  // A new state has begun at 'now' microseconds
  pulse_start = now;
  state       = new_state;
}

#define SEND_BIT(byte, bit)                                                  \
{                                                                            \
  unsigned int bit_time = (byte & (1 << bit)) ? BIT_1_LENGTH : BIT_0_LENGTH; \
  *out = high;                                                               \
  delayMicroseconds(bit_time);                                               \
  *out = low;                                                                \
  delayMicroseconds(SPACE_LENGTH);                                           \
}

#define SEND_BYTE(data, offset) \
{                               \
  uint8_t byte = data[offset];  \
  SEND_BIT(byte, 0)             \
  SEND_BIT(byte, 1)             \
  SEND_BIT(byte, 2)             \
  SEND_BIT(byte, 3)             \
  SEND_BIT(byte, 4)             \
  SEND_BIT(byte, 5)             \
  SEND_BIT(byte, 6)             \
  SEND_BIT(byte, 7)             \
}

// A very simple self-explanatory data send routine
static void send(const uint8_t* data) {
  uint8_t bit  = digitalPinToBitMask(TX_PIN);
  uint8_t port = digitalPinToPort(TX_PIN);
  volatile uint8_t *out = portOutputRegister(port);
  uint8_t high = *out | bit;
  uint8_t low = high & ~bit;
	uint8_t oldSREG = SREG;

  // Comment out this cli() in order to enable loopback testing
  cli();

  *out = low;
  delayMicroseconds(SYNC_LENGTH); // SYNC low
  *out = high;
  delayMicroseconds(START_LENGTH); // Start bit high
  *out = low;
  delayMicroseconds(SPACE_LENGTH); // Space low
  // Extremely unrolled for better precision
  SEND_BYTE(data, 0)
  SEND_BYTE(data, 1)
  SEND_BYTE(data, 2)
  SEND_BYTE(data, 3)
  SEND_BYTE(data, 4)
  SEND_BYTE(data, 5)
  SEND_BYTE(data, 6)
  SEND_BYTE(data, 7)
  *out = high; // Delay high
  delayMicroseconds(END_DELAY);
  *out = low; // Terminator low
  delayMicroseconds(END_LENGTH);
  *out = high; // Idle high

  SREG = oldSREG; // Restore interrupts
}

static Receiver rx1(RX1_PIN);

// Our interrupt handlers don't get any arguments, so we have to provide these bridges
static void rx1PinInt() {
  rx1.onInterrupt();
}

#ifdef RX2_PIN
static Receiver rx2(RX2_PIN);

static void rx2PinInt() {
  rx2.onInterrupt();
}
#endif

// Checksum calculation
static uint8_t calcCRC(const uint8_t* data) {
  uint8_t last_nibble = data[CRC_OFFSET] & 0x0F;
  // 4-bit sum of all nibbles, including the last one
  uint8_t crc = last_nibble;

  for (int i = 0; i < CRC_OFFSET; i++) {
    crc += (data[i] >> 4) + (data[i] & 0x0F);
  }

  // The received packet contains the CRC in high nibble. Low nibble is
  // also part of the payload. Keep it in place for easier manipulation
  return (crc << 4) | last_nibble;
}

static bool setCRC(uint8_t* data) {
  data[CRC_OFFSET] = calcCRC(data);
}

static void dump(const char* prefix, const uint8_t* buffer, int len) {
  Serial.print(prefix);

  for (int i = 0; i < len; i++) {
    Serial.print(' ');
    if (buffer[i] < 0x10)
      Serial.print('0');
    Serial.print(buffer[i], HEX);
  }
}

static void printTimestamp(unsigned long ts) {
  char buffer[16];

  sprintf(buffer, "%010lu ", ts);
  Serial.print(buffer);
}

static void dump(const char* prefix, const Receiver& rx) {
  printTimestamp(rx.getRxTimestamp());
  // Dump the whole thing, including start bit and CRC
  dump(prefix, rx.getBuffer(), RX_LEN);

  const uint8_t* payload = rx.getData();
  uint8_t crc = calcCRC(payload);

  if (crc == payload[CRC_OFFSET]) {
    Serial.println(" OK");
  } else {
    Serial.print(" BAD ");
    Serial.println(crc >> 4, HEX);
  }
}

TerminalInput<32> termin;

// Packet, pending for send
uint8_t tx_buffer[PKT_LEN];
bool send_pending = false;

static uint8_t parseHexDigit(char c) {
  return ((c < 'A') ? c - '0' : toupper(c) - 'A' + 10);
}

static int parseHex(const char* txt, const char** endp) {
  int v = -1;

  if (isxdigit(*txt)) {
    v = parseHexDigit(*txt++);
    if (isxdigit(*txt)) {
      v = (v << 4) | parseHexDigit(*txt++);
    }
  }

  *endp = txt;
  return v;
}

static void handleCommand(const char* line) {
  const char *p = line;
  uint8_t hex_buffer[PKT_LEN];
  int repeat;

  // Command line format is:
  // NN NN NN NN NN NN NN NN (hex bytes) - send the data
  // Rx NN NN NN NN NN NN NN NN - send the data, repeat x times (one digit only)
  // 
  if (toupper(p[0]) == 'R') {
    if (p[1] < '1' || p[1] > '9') {
      Serial.print("Bad repeat count: ");
      Serial.println(line);
      return;
    }
    repeat = p[1] - '0';
    p += 2;
  } else {
    repeat = 1; // Send once by default
  }

  for (int i = 0; i < PKT_LEN; i++) {
    // Skip spaces if any
    while (*p == ' ')
      p++;

    int byte = parseHex(p, &p);

    if (byte == -1) {
      Serial.print("Bad hex data: ");
      Serial.println(line);
      return;
    }

    hex_buffer[i] = byte;
  }

  // Queue the packet
  memcpy(tx_buffer, hex_buffer, PKT_LEN);
  setCRC(tx_buffer);
  send_pending = true;
}

void setup() {
  Serial.begin(115200);

  rx1.begin(rx1PinInt);
#ifdef RX2_PIN
  rx2.begin(rx2PinInt);
#endif

  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, 1);
}

void loop() {
  unsigned long ack_ts;

  if (rx1.getAck(ack_ts)) {
    printTimestamp(ack_ts);
    Serial.println("Rx1 _");
  }
  if (rx1.isDataReady()) {
    dump("Rx1", rx1);
    rx1.reset();
  }
#ifdef RX2_PIN
  if (rx2.getAck(ack_ts)) {
    printTimestamp(ack_ts);
    Serial.println("Rx2 _");
  }
  if (rx2.isDataReady()) {
    dump("Rx2", rx2);
    rx2.reset();
  }
#endif
  if (const char *line = termin.getLine()) {
    handleCommand(line);
  }
  // Only send our pending data when the other side is not busy transmitting.
  // Daichi controller is known to choke if this is violated; we assume that all
  // CN_WIRED devices follow this rule. This also explains why the send() routine
  // appears to work fine with enabled interrupts.
  if (send_pending && !rx1.isBusy()) {
    // Unwinding the first iteration this way avoids unwanted delay
    // before first of after last iteration
    send(tx_buffer);
    // Respond back after actually sending the packet
    dump("Tx", tx_buffer, PKT_LEN);
    Serial.println();
    // Sent
    send_pending = 0;
  }

  yield();
}
