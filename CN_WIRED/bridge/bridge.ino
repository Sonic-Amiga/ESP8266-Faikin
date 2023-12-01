// Pins we're using. RX2 is optional, can be commented out if not used
#define RX1_PIN 2 // PD3
#define RX2_PIN 3 // PD2
#define TX_PIN  4 // PD4

// Data packet length
#define PKT_LEN 8
// The packet contains a 4-bit checksum in its last byte
#define CRC_OFFSET PKT_LEN - 1
// Our RX buffers are prefixed by one byte, where start bit is stored.
// See start()
#define RX_LEN PKT_LEN + 1

class Receiver {
  public:
    Receiver(int pin) : pin(pin) {}

    void begin(void(*handler)()) {
      state = 1;
      pinMode(pin, INPUT);
      attachInterrupt(digitalPinToInterrupt(pin), handler, CHANGE);
    }

    void reset() {
      rx_bytes = -1; // This signals "Wait for sync" state
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

    unsigned long getTimestamp() const {
      return last_rx;
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
    unsigned long last_rx;     // Timestamp of the received packet
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
    if (now - pulse_start > 2000) {
      // Got SYNC pulse, start receiving data
      last_rx = pulse_start;
      start();
    }
  } else {
    // HIGH->LOW, start of SYNC or end of data bit
    if (isReceiving()) {
      // High bit - 1000us, low bit - 400us
      uint8_t bit = (now - pulse_start) > 700;
      receiveBit(bit);
    }
  }

  // A new state has begun at 'now' microseconds
  pulse_start = now;
  state       = new_state;
}

// A very simple self-explanatory data send routine
static void send(const uint8_t* data, int len) {
  digitalWrite(TX_PIN, 0);
  delayMicroseconds(2600); // SYNC low
  digitalWrite(TX_PIN, 1);
  delayMicroseconds(1000); // 1 high
  digitalWrite(TX_PIN, 0);
  delayMicroseconds(300);  // Space low

  for (int i = 0; i < len; i++) {
    for (int b = 0; b < 8; b++) {
      uint8_t v = data[i] & (1 << b);
      digitalWrite(TX_PIN, 1);
      delayMicroseconds(v ? 900 : 400); // Bit high
      digitalWrite(TX_PIN, 0);
      delayMicroseconds(300); // Space low
    }
  }

  digitalWrite(TX_PIN, 1); // Idle high
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

/*** Serial terminal stuff begins here ***/

// TX buffer, where we collect data from the serial port
// Similar to RX, reserve one byte in tne end in order to avoid extra checks
static uint8_t tx_buffer[PKT_LEN + 1];
static int tx_bytes;
static int tx_bits;

static void resetTx() {
  tx_bytes     = 0;
  tx_bits      = 4;
  tx_buffer[0] = 0;
}

static void queueDigit(uint8_t v) {
  if (tx_bytes == PKT_LEN) {
    return; // Simple overflow prevention
  }
  tx_buffer[tx_bytes] |= (v << tx_bits);
  tx_bits ^= 4;
  if (tx_bits) {
    tx_bytes++;
    tx_buffer[tx_bytes] = 0; // This is why we reserve an extra byte
  }
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

static void dump(const char* prefix, const Receiver& rx) {
  char buffer[16];

  sprintf(buffer, "%010lu ", rx.getTimestamp());
  Serial.print(buffer);
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

void setup() {
  Serial.begin(115200);

  rx1.begin(rx1PinInt);
#ifdef RX2_PIN
  rx2.begin(rx2PinInt);
#endif

  resetTx();
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, 1);
}

void loop() {
  if (rx1.isDataReady()) {
    dump("Rx1", rx1);
    rx1.reset();
  }
#ifdef RX2_PIN
  if (rx2.isDataReady()) {
    dump("Rx2", rx2);
    rx2.reset();
  }
#endif
  if (Serial.available() > 0) {
    int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (tx_bytes == PKT_LEN) {
        // Only send complete packets
        setCRC(tx_buffer);
        dump("Tx", tx_buffer, PKT_LEN);
        Serial.println("");
        send(tx_buffer, PKT_LEN);
      } else {
        Serial.print("Bad length: ");
        Serial.println(tx_bytes);
      }
      resetTx();
    } else if (isxdigit(c)) {
      uint8_t v = (c < 'A') ? c - '0' : toupper(c) - 'A' + 10;
      queueDigit(v);
    }
  }
  // Wait for interrupt here
}
