#define RX_PIN 3 // PD2
#define TX_PIN 2 // PD3

static int rx_state;
static unsigned long start;

#define PKT_LEN 8
#define RX_LEN PKT_LEN + 1

static uint8_t rx_buffer[RX_LEN + 1];
static int rx_bytes;
static int rx_bits;

static uint8_t tx_buffer[PKT_LEN + 1];
static int tx_bytes;
static int tx_bits;

static void receiveBit(uint8_t v) {
  rx_buffer[rx_bytes] |= (v << rx_bits);
  if (rx_bits == 7) {
    rx_bits = 0;
    rx_bytes++;
    rx_buffer[rx_bytes] = 0;
  } else {
    rx_bits++;
  }
}

static void resetRx() {
  rx_bytes = -1;
}

static void startRx() {
  // The packet always starts with a single bit of 1, which is apparently
  // a start bit, so the 0th byte will contain this single bit. This also lets
  // us make sure that we're correct, and it's indeed always 1.
  // The next bit is going to be LSB of 1st byte.
  rx_bytes     = 0;
  rx_bits      = 7;
  rx_buffer[0] = 0;
}

static bool isReceiving() {
  return rx_bytes >= 0 && rx_bytes < RX_LEN;
}

static bool isDataReady() {
  return rx_bytes == RX_LEN;
}

void rxPinInt() {
  int new_state = digitalRead(RX_PIN);

  if (new_state == rx_state) {
    return; // Some rubbish
  }

  unsigned long now = micros();

  if (new_state) {
    // LOW->HIGH, start of data bit
    if (now - start > 2000) {
      startRx(); // Got SYNC pulse, start receiving data
    }
  } else {
    // HIGH->LOW, start of SYNC or end of data bit
    if (isReceiving()) {
      // High bit - 1000us, low bit - 400us
      uint8_t bit = (now - start) > 700;
      receiveBit(bit);
    }
  }

  start = now;
  rx_state = new_state;
}

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

void setup() {
  Serial.begin(115200);

  rx_state = 1;
  resetRx();
  resetTx();

  pinMode(RX_PIN, INPUT);
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, 1);
  attachInterrupt(digitalPinToInterrupt(RX_PIN), rxPinInt, CHANGE);
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
  if (isDataReady()) {
    Serial.print("Rx ");
    dump(rx_buffer, RX_LEN);
    resetRx();
  }
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
