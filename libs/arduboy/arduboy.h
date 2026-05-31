#ifndef ARDUBOY_H
#define ARDUBOY_H

#include <stdint.h>
#include <stdbool.h>
#include "arduino_compat.h"
#include "arduboy_tunes.h"

typedef struct app_context app_context_t;

#define LEFT_BUTTON   (1 << 0)
#define RIGHT_BUTTON  (1 << 1)
#define UP_BUTTON     (1 << 2)
#define DOWN_BUTTON   (1 << 3)
#define A_BUTTON      (1 << 4)
#define B_BUTTON      (1 << 5)

#define WIDTH  128
#define HEIGHT 64

class Arduboy {
public:
    uint8_t frameRate;
    uint32_t frameStartTime;
    bool nextFrameReady;

    uint8_t textSize;
    int cursorX;
    int cursorY;
    uint8_t textColor;
    uint8_t textBackground;

    uint8_t currentButtonState;
    uint8_t previousButtonState;

    bool initialized;

    ArduboyTunes tunes;

    struct Audio {
        bool enabled;
        void on() { enabled = true; }
        void off() { enabled = false; }
        Audio() : enabled(false) {}
    } audio;

    void begin();
    void clear();
    void display();
    void setFrameRate(uint8_t fps);
    bool nextFrame();
    void setTextSize(uint8_t size);
    void setCursor(int x, int y);
    void setTextColor(uint8_t color);
    void setTextBackground(uint8_t color);
    void print(const char *text);
    void print(int val);
    void drawPixel(int x, int y, uint8_t color);
    void drawRect(int x, int y, int width, int height, uint8_t color);
    void fillRect(int x, int y, int width, int height, uint8_t color);
    void fillCircle(int x, int y, int radius, uint8_t color);
    bool pressed(uint8_t buttons);
    bool notPressed(uint8_t buttons);

    void drawSlowXYBitmap(int x, int y, const uint8_t *bitmap, int w, int h, uint8_t color);
    void drawFastHLine(int x, int y, int w, uint8_t color);
    void drawFastVLine(int x, int y, int h, uint8_t color);
    void pollButtons();
    bool anyPressed(uint8_t buttons);
    bool justPressed(uint8_t buttons);
    void initRandomSeed();

    void initInput();
    void updateInput();
    void setAppContext(app_context_t *ctx);
    bool exitRequested();
};

class Sprites {
public:
    static void drawOverwrite(int x, int y, const uint8_t *sprite, uint8_t frame);
};

void arduboy_call_setup(void);
void arduboy_call_loop(void);
void arduboy_handle_key_event(char key, bool pressed);
void arduboy_handle_touch_event(int x, int y, bool pressed);
void arduboy_cleanup(void);

#endif // ARDUBOY_H