// Pins we're using.
#define RX1_PIN 2 // PD2
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
#define ACK_LENGTH   2000
// Detection tolerance
#define THRESHOLD 200

static uint8_t temp_response[PKT_LEN];

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
    if (now - pulse_start > SYNC_LENGTH - THRESHOLD) {
      // Got SYNC pulse, start receiving data
      start();
    }
  } else {
    // HIGH->LOW, start of SYNC or end of data bit
    if (isReceiving()) {
      // High bit - 900us, low bit - 400us
      uint8_t bit = (now - pulse_start) > BIT_1_LENGTH - THRESHOLD;
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
  *out = high; // Idle high
  delayMicroseconds(16000);
  *out = low;
  delayMicroseconds(ACK_LENGTH); // ACK low
  *out = high; // Idle high

  SREG = oldSREG; // Restore interrupts
}

static Receiver rx1(RX1_PIN);

// Our interrupt handlers don't get any arguments, so we have to provide these bridges
static void rx1PinInt() {
  rx1.onInterrupt();
}

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

static void setCRC(uint8_t* data) {
  data[CRC_OFFSET] = calcCRC(data);
}

#define TERM_BUFFER_SIZE 10

class SerialTextInput {
 public:
    const char *getBuffer() const {
      return buffer;
    }

    void reset() {
      rx_bytes = 0;
    }

    bool lineReady();

 private:
    char buffer[TERM_BUFFER_SIZE + 1];
    int  rx_bytes = 0;
};

bool SerialTextInput::lineReady() {
  if (Serial.available() > 0) {
    int c = Serial.read();

    if (c == '\r') {
      Serial.println();
      if (rx_bytes) {
        buffer[rx_bytes] = 0;
        rx_bytes = TERM_BUFFER_SIZE; // Prevent further read till reset
        return true;
      }
    } else if (rx_bytes < TERM_BUFFER_SIZE) {
      buffer[rx_bytes++] = c;
      Serial.write(c); // Echo
    }        
  }
  return false;
}

SerialTextInput term;

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

static void setIndoorsTemp(uint8_t indoors_temp) {
  temp_response[0] = indoors_temp; // BCD
  setCRC(temp_response);
}

void setup() {
  temp_response[0] = 0x20;
  temp_response[1] = 0x04;
  temp_response[2] = 0x50;
  temp_response[3] = 0x00;
  temp_response[4] = 0x00;
  temp_response[5] = 0x00;
  temp_response[6] = 0x00;
  temp_response[7] = 0x00;
  setCRC(temp_response);

  Serial.begin(115200);

  rx1.begin(rx1PinInt);

  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, 1);
}

static void help() {
  Serial.println("? - this help");
  Serial.println("T <decimal> - Set indoors temperature in C");
}

#define BAD_BCD 0xFF

static uint8_t get_bcd(const char *text) {
  if (text[0] == 0) {
    return BAD_BCD;
  }
  if (!isdigit(text[0])) {
    return BAD_BCD;
  }
  uint8_t d1 = text[0] - '0';
  if (text[1] != 0) {
    if (!isdigit(text[1])) {
      return BAD_BCD;
    }
    uint8_t d2 = isdigit(text[1]) ? text[1] - '0' : BAD_BCD;
    return (d1 << 4) | d2;
  }
  return d1;
}

void loop() {
  if (rx1.isDataReady()) {
    dump("Rx1", rx1);
    rx1.reset();
    delayMicroseconds(1000);
    send(temp_response);
  }
  if (term.lineReady()) {
    const char *user_cmd = term.getBuffer();

    if (user_cmd[0] == '?') {
      help();
    } else if (user_cmd[0] == 'T') {
      uint8_t indoors = get_bcd(user_cmd + 1);

      if (indoors != BAD_BCD) {
        setIndoorsTemp(indoors);
      } else {
        Serial.println("Bad value");
      }
    } else {
      Serial.print("Bad command: ");
      Serial.println(user_cmd);
    }

    term.reset();
  }
  yield();
}
