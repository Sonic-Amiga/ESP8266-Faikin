#include "terminal_in.hpp"

// Pins we're using. RX2 is optional, can be commented out if not used
#define RX1_PIN 2 // PD2
//#define RX2_PIN 3 // PD3
#define TX_PIN  4 // PD4

// Uncomment this to enable timings dump. Impacts I/O performance.
//#define DUMP_TIMINGS

// Data packet length
#define PKT_LEN 8
// The packet contains a 4-bit checksum in its last byte
#define CRC_OFFSET PKT_LEN - 1

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

// We also want to provide raw timings info for calibrating transmitter implementations,
// so our Receiver stores raw pulse timings, then we decode them using decodeData().
#define NUM_SAMPLES 3 + PKT_LEN * 16

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
      samples = -1; // This signals "Wait for sync" state
    }

    bool isBusy() const {
      // isReceiving state is only entered after SYNC pulse has ended.
      // Checking for !state covers the SYNC and the final space.
      return !state || isReceiving();
    }

    bool isReceiving() const {
      return samples >= 0 && samples < NUM_SAMPLES;
    }

    bool isDataReady() const {
      return samples == NUM_SAMPLES;
    }

    // For "not ready" dumps
    unsigned int getLineState() const {
      return state;
    }

    const unsigned long* getBuffer() const {
      return buffer;
    }

    unsigned long getRxTimestamp() const {
      return packet_start;
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

    void decodeData(uint8_t *buffer) const;

    void onInterrupt();

  private:
    void start() {
      samples = 0;
    }

    unsigned long buffer[NUM_SAMPLES];
    unsigned long packet_start; // Packet timestamp
    unsigned long pulse_start;  // Pulse start time
    unsigned int  state;        // Current line state
    int           samples;      // Counter of collected pulses
    volatile bool ack;
    unsigned long last_ack;    // Timestamp of the last ACK pulse
    const int     pin;
};

static bool match_pulse(unsigned long len, unsigned long ref)
{
  return (len > ref - THRESHOLD) && (len < ref + THRESHOLD);
}

void Receiver::onInterrupt() {
  int new_state = digitalRead(pin);

  if (new_state == state) {
    return; // Some rubbish
  }

  unsigned long now = micros();
  unsigned long duration = now - pulse_start;

  if (new_state) {
    // LOW => HIGH
    if (match_pulse(duration, SYNC_LENGTH)) {
      // Got SYNC pulse, start receiving data
      packet_start = pulse_start;
      start();
    } else if (match_pulse(duration, END_LENGTH)) {
      // Got ACK pulse
      if (!ack) {
        last_ack = pulse_start;
        ack = true;
      }
    }
  }

  if (isReceiving()) {
    buffer[samples++] = duration;
  }

  // A new state has begun at 'now' microseconds
  pulse_start = now;
  state       = new_state;
}

void Receiver::decodeData(uint8_t *dest) const {
  int rx_bytes = 0;
  int rx_bits  = 0;

  dest[0] = 0;

  for (int i = 3; i < NUM_SAMPLES; i += 2) {
    // High bit - 900us, low bit - 400us
    uint8_t v = buffer[i] > (BIT_1_LENGTH - THRESHOLD);

    dest[rx_bytes] |= (v << rx_bits);

    if (rx_bits == 7) {
      rx_bits = 0;
      rx_bytes++;
      // Prepare the next byte if present.
      if (rx_bytes < PKT_LEN)
        dest[rx_bytes] = 0;
    } else {
      rx_bits++;
    }
  }
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

// Data send routine. Extremely unrolled by hands for acheiving timings as precise as possible.
static void send(const uint8_t* data, bool end_pulse) {
  // Pre-compute everything we need for GPIO manipulations.
  // What we're doing here duplicates inner guts of digitalWrite(). We do it so that
  // the only remaining thing is actual register write.
  uint8_t bit  = digitalPinToBitMask(TX_PIN);
  uint8_t port = digitalPinToPort(TX_PIN);
  volatile uint8_t *out = portOutputRegister(port);
  // We're not doing any concurrent output from within interrupts, so we can compute
  // bit masks ouitside of cli()
  uint8_t high = *out | bit;
  uint8_t low = high & ~bit;
  // Preserve original state of interrupts
  uint8_t oldSREG = SREG;

  // Disable interrupts. Comment this out in order to enable loopback testing.
  cli();

  *out = low;
  delayMicroseconds(SYNC_LENGTH); // SYNC low
  *out = high;
  delayMicroseconds(START_LENGTH); // Start bit high
  *out = low;
  delayMicroseconds(SPACE_LENGTH); // Space low
  // Send our 8 bytes. Loops unrolled by hands.
  SEND_BYTE(data, 0)
  SEND_BYTE(data, 1)
  SEND_BYTE(data, 2)
  SEND_BYTE(data, 3)
  SEND_BYTE(data, 4)
  SEND_BYTE(data, 5)
  SEND_BYTE(data, 6)
  SEND_BYTE(data, 7)
  if (end_pulse) {
    *out = high;
    delayMicroseconds(END_DELAY); // Delay high
    *out = low;
    delayMicroseconds(END_LENGTH); // END low
  }
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
  unsigned long timestamp = rx.getRxTimestamp();
  char payload[PKT_LEN];

  rx.decodeData(payload);


#ifdef DUMP_TIMINGS
  printTimestamp(timestamp);
  Serial.print(prefix);
  Serial.print(" T");
  for (int i = 0; i < NUM_SAMPLES; i++) {
    Serial.print(' ');
    Serial.print(rx.getBuffer()[i]);
  }
  Serial.println();
#endif
  rx.reset();

  uint8_t crc = calcCRC(payload);

  printTimestamp(timestamp);
  // Dump the whole thing, including start bit and CRC
  dump(prefix, payload, PKT_LEN);

  if (crc == payload[CRC_OFFSET]) {
    Serial.println(" OK");
  } else {
    Serial.print(" BAD ");
    Serial.println(crc >> 4, HEX);
  }
}

TerminalInput<32> termin;

// Packet, pending for send
uint8_t       tx_buffer[PKT_LEN];
bool          tx_end_pulse;
bool          send_pending = false;
unsigned long send_timestamp;

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

  // Command line format is:
  // NN NN NN NN NN NN NN NN _ (hex bytes) - send the data
  // Underscore character is optional, it tells us to send "END" pulse of 2 ms
  // after the packet. This pulse is a part of official CN_WIRED protocol, but some
  // 3rd party devices don't seem to send it, so we leave this option for research.
  // Spaces are ignored, and are present only for user's convenience
  for (int i = 0; i < PKT_LEN; i++) {
    // Skip spaces if any
    while (*p == ' ')
      p++;

    int byte = parseHex(p, &p);

    if (byte == -1) {
      Serial.print("Bad hex data at ");
      Serial.print(p - line);
      Serial.print(": ");
      Serial.println(line);
      return;
    }

    hex_buffer[i] = byte;
  }

  // Skip spaces if any
  while (*p == ' ')
    p++;

  // Queue the packet
  memcpy(tx_buffer, hex_buffer, PKT_LEN);
  setCRC(tx_buffer);
  // '_' postfix is used to tell the bridge to send a terminating 2ms LOW pulse
  tx_end_pulse = *p == '_';
  send_pending = true;
  send_timestamp = millis();
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
  }
#ifdef RX2_PIN
  if (rx2.getAck(ack_ts)) {
    printTimestamp(ack_ts);
    Serial.println("Rx2 _");
  }
  if (rx2.isDataReady()) {
    dump("Rx2", rx2);
  }
#endif
  if (const char *line = termin.getLine()) {
    handleCommand(line);
  }
  if (send_pending) {
    // Only send our pending data when the other side is not busy transmitting.
    // Daichi controller is known to choke if this is violated; we assume that all
    // CN_WIRED devices follow this rule. This also explains why the send() routine
    // appears to work fine with enabled interrupts.
    if (rx1.isBusy()) {
      if (millis() - send_timestamp > 5000) {
        // If the receiver is locked up during prolinged time (5 seconds),
        // it's a good idea to tell.
        send_timestamp = millis();
        Serial.print("ERR: Receiver not ready! Line state = ");
        Serial.println(rx1.getLineState());
      }
    } else {
      // Unwinding the first iteration this way avoids unwanted delay
      // before first of after last iteration
      send(tx_buffer, tx_end_pulse);
      // Respond back after actually sending the packet
      dump("Tx", tx_buffer, PKT_LEN);
      if (tx_end_pulse)
        Serial.print('_');
      Serial.println();
      send_pending = false; // Sent
    }
  }

  yield();
}
