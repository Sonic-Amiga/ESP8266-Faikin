// Pins we're using. RX2 is optional, can be commented out if not used
#define RX1_PIN 2 // PD3
#define RX2_PIN 3 // PD2
#define TX_PIN  4 // PD4

#define PKT_LEN 8
// Our RX buffers are prefixed by one byte, where start bit is stored.
// See start()
#define RX_LEN  PKT_LEN + 1

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
      start(); // Got SYNC pulse, start receiving data
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
  delayMicroseconds(2500); // SYNC low
  digitalWrite(TX_PIN, 1);
  delayMicroseconds(1000); // 1 high
  digitalWrite(TX_PIN, 0);
  delayMicroseconds(300);  // Space low

  for (int i = 0; i < len; i++) {
    for (int b = 0; b < 8; b++) {
      uint8_t v = data[i] & (1 << b);
      digitalWrite(TX_PIN, 1);
      delayMicroseconds(v ? 1000 : 400); // Bit high
      digitalWrite(TX_PIN, 0);
      delayMicroseconds(300); // Space low
    }
  }

  digitalWrite(TX_PIN, 1); // Idle high
}

static Receiver rx1(RX1_PIN);
static Receiver rx2(RX2_PIN);

// TX buffer, where we read data from the serial port
static uint8_t tx_buffer[PKT_LEN + 1];
static int tx_bytes;
static int tx_bits;

static void resetTx() {
  tx_bytes = 0;
  tx_bits  = 4;
  memset(tx_buffer, 0, PKT_LEN);
}

static void queueDigit(uint8_t v) {
  if (tx_bytes == PKT_LEN) {
    return;
  }
  tx_buffer[tx_bytes] |= (v << tx_bits);
  tx_bits ^= 4;
  if (tx_bits) {
    tx_bytes++;
  }
}

// Our interrupt handlers don't get any arguments, so we have to provide these bridges
static void rx1PinInt() {
  rx1.onInterrupt();
}

#ifdef RX2_PIN
static void rx2PinInt() {
  rx2.onInterrupt();
}
#endif

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

static void dump(const uint8_t* buffer, int len) {
  for (int i = 0; i < len; i++) {
    if (buffer[i] < 0x10)
      Serial.print('0');
    Serial.print(buffer[i], HEX);
    Serial.print(' ');
  }
  Serial.println("");
}

void loop() {
  if (rx1.isDataReady()) {
    Serial.print("Rx1 ");
    dump(rx1.getBuffer(), RX_LEN);
    rx1.reset();
  }
#ifdef RX2_PIN
  if (rx2.isDataReady()) {
    Serial.print("Rx2 ");
    dump(rx2.getBuffer(), RX_LEN);
    rx2.reset();
  }
#endif
  if (Serial.available() > 0) {
    int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (tx_bytes > 0) {
        Serial.print("Tx ");
        dump(tx_buffer, tx_bytes);
        send(tx_buffer, tx_bytes);
        resetTx();
      }
    } else if (isxdigit(c)) {
      uint8_t v = (c < 'A') ? c - '0' : tolower(c) - 'A' + 10;
      queueDigit(v);
    }
  }
  // Wait for interrupt here
}
