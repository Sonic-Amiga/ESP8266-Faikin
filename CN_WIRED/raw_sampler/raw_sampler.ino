// Pins we're using. RX2 is optional, can be commented out if not used
#define RX1_PIN 2 // PD2

#define NUM_SAMPLES 131

struct Sample {
  int           state;
  unsigned long duration;
};

class Receiver {
  public:
    Receiver(int pin) : pin(pin) {}

    void begin(void(*handler)()) {
      state = 1;
      pinMode(pin, INPUT);
      attachInterrupt(digitalPinToInterrupt(pin), handler, CHANGE);
    }

    void reset() {
      samples = -1; // This signals "Wait for sync" state
    }

    bool isReceiving() const {
      return samples >= 0 && samples < NUM_SAMPLES;
    }

    bool isDataReady() const {
      return samples == NUM_SAMPLES;
    }

    const Sample* getData() const {
      return buffer;
    }

    void onInterrupt();

  private:
    void start() {
      samples = 0;
    }

    Sample        buffer[NUM_SAMPLES];
    unsigned long pulse_start; // Pulse start time
    unsigned int  state;       // Current line state
    int           samples;
    const int     pin;
};

void Receiver::onInterrupt() {
  int new_state = digitalRead(pin);

  if (new_state == state) {
    return; // Some rubbish
  }

  unsigned long now = micros();
  unsigned long duration = now - pulse_start;

  if (new_state && duration > 2000) {
    // Got SYNC pulse, start receiving data
    start();
  }

  if (isReceiving()) {
    buffer[samples].state    = state;
    buffer[samples].duration = duration;
    samples++;
  }

  // A new state has begun at 'now' microseconds
  pulse_start = now;
  state       = new_state;
}

static Receiver rx1(RX1_PIN);

// Our interrupt handlers don't get any arguments, so we have to provide these bridges
static void rx1PinInt() {
  rx1.onInterrupt();
}

void setup() {
  Serial.begin(115200);
  rx1.begin(rx1PinInt);
}

void loop() {
  if (rx1.isDataReady()) {
    const Sample* data = rx1.getData();

    for (int i = 0; i < NUM_SAMPLES; i++) {
      Serial.print(data[i].state);
      Serial.print(' ');
      Serial.print(data[i].duration);
      Serial.print(' ');
    }
    Serial.println("");
    rx1.reset();
  }
}
