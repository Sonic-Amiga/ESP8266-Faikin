#ifndef TERMINAL_IN
#define TERMINAL_IN

// N - maximum length of the line, including null terminator (will be appended)
template <int N>
class TerminalInput
{
  public:
    const char *getLine()
    {
        if (Serial.available() <= 0)
            return nullptr;
        int c = Serial.read();
        if (c == '\n' || c == '\r') {
            // Enter key has arrived
            if (length) {
                buffer[length] = 0; // null-terminate the line
                length = 0;         // Next loop will restart from scratch
                return buffer;
            }
        } else if (length < N - 1) {
            buffer[length++] = c;
        }

        return nullptr;
    }

  private:
    char         buffer[N];
    unsigned int length = 0;
};

#endif