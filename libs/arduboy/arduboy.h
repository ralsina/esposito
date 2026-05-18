#ifndef ARDUBOY_H
#define ARDUBOY_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration
typedef struct app_context app_context_t;

// Arduboy button constants
#define LEFT_BUTTON   (1 << 0)
#define RIGHT_BUTTON  (1 << 1)
#define UP_BUTTON     (1 << 2)
#define DOWN_BUTTON   (1 << 3)
#define A_BUTTON      (1 << 4)
#define B_BUTTON      (1 << 5)

// Arduboy display constants
#define WIDTH  128
#define HEIGHT 64

// C++ Arduboy class (mimics original Arduino API)
class Arduboy {
public:
    // Frame timing
    uint8_t frameRate;
    uint32_t frameStartTime;
    bool nextFrameReady;

    // Display state
    uint8_t textSize;
    int cursorX;
    int cursorY;

    // Input state
    uint8_t currentButtonState;
    uint8_t previousButtonState;

    // Initialization state
    bool initialized;

    // Core API methods (that match original Arduboy class)
    void begin();
    void clear();
    void display();
    void setFrameRate(uint8_t fps);
    bool nextFrame();
    void setTextSize(uint8_t size);
    void setCursor(int x, int y);
    void print(const char *text);
    void print(int val);
    void fillCircle(int x, int y, int radius, uint8_t color);
    bool pressed(uint8_t buttons);

    // Helper functions for Esposito integration
    void initInput();
    void updateInput();
    void setAppContext(app_context_t *ctx);
};

// Helper functions for Esposito integration
void arduboy_call_setup(void);
void arduboy_call_loop(void);
void arduboy_handle_key_event(char key, bool pressed);
void arduboy_handle_touch_event(int x, int y, bool pressed);

#endif // ARDUBOY_H