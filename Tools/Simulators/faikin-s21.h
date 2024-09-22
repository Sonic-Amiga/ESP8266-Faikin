struct S21State
{
    int          power;    // Power on
    int          mode;     // Mode
    float        temp;     // Set point
    int          fan;      // Fan speed
    int          swing;    // Swing direction
    int          humidity; // Humidity setting
    int          powerful; // Powerful mode
    int          comfort;  // Comfort mode
    int          quiet;    // Quiet mode
    int          sensor;   // Sensor mode
    int          led;      // LED on/off
    int          streamer; // Streamer mode
    int          eco;      // Eco mode
    int          demand;   // Demand setting
    int          home;     // Reported temparatures (multiplied by 10 here)
    int          outside;
    int          inlet;
    unsigned int  fanrpm;         // Fan RPM (divided by 10 here)
    unsigned int  comprpm;        // Compressor RPM
    unsigned int  consumption;    // Power consumption
    unsigned char protocol_major; // Protocol version
    unsigned char protocol_minor;
    char          model[4];       // Reported A/C model code
    // The following aren't understood yet
    unsigned char F2[4];
    unsigned char F3[4];
    unsigned char F4[4];
    unsigned char FB[4];
    unsigned char FG[4];
    unsigned char FK[4];
    unsigned char FN[4];
    unsigned char FP[4];
    unsigned char FQ[4];
    unsigned char FR[4];
    unsigned char FS[4];
    unsigned char FT[4];
    unsigned char FV[4];
    unsigned char M[4];
    unsigned char V[4];
    unsigned char VS000M[14];
    unsigned char FU00[32];
    unsigned char FU02[32];
    unsigned char FU04[32];
    unsigned char FY10[8];
    unsigned char FY20[4];
};

// POSIX shm requires the name to start with '/' for portability reasons.
// Works also on Windows with no problems, so let it be
#define SHARED_MEM_NAME "/Faikin-S21"

void state_options_help(void);
int parse_item(int argc, const char **argv, struct S21State *state);
