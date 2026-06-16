#include "os_core.h"
#include "text_mode.h"
#include "graphics_mode.h"
#include "hardware.h"
#include "app_config.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Forward declarations of app functions
extern void app_init(app_context_t *ctx);
extern void app_event(app_context_t *ctx, event_t *event);
extern void app_checkpoint(app_context_t *ctx);
extern void app_close(app_context_t *ctx);

// Translate SDL keycode to ASCII
static char sdl_key_to_ascii(SDL_Keycode sym, SDL_Keymod mod) {
    // Handle printable characters via SDL
    if (sym >= SDLK_SPACE && sym <= SDLK_z) {
        char ch = (char)sym;
        if ((mod & KMOD_LSHIFT) || (mod & KMOD_RSHIFT)) {
            if (ch >= 'a' && ch <= 'z') return ch - 32;
            // For uppercase symbols, SDL provides uppercase in sym when shift is held
            // Actually SDL_Keycode is already the shifted value for printable keys
            return ch;
        }
        // If shift not held and this is uppercase, convert to lowercase
        if (ch >= 'A' && ch <= 'Z') return ch + 32;
        return ch;
    }
    // Handle special keys
    switch (sym) {
        case SDLK_RETURN:    return '\r';
        case SDLK_TAB:       return '\t';
        case SDLK_BACKSPACE: return '\b';
        case SDLK_ESCAPE:    return 0x1B;
        case SDLK_UP:        return 0x99;
        case SDLK_DOWN:      return 0x98;
        case SDLK_LEFT:      return 0x97;
        case SDLK_RIGHT:     return 0x96;
        case SDLK_DELETE:    return 0x93;
        default:             return 0;
    }
}

// Map SDL keycodes to Esposito raw key codes
static uint8_t sdl_to_raw_key(SDL_Keycode sym) {
    if (sym >= SDLK_F1 && sym <= SDLK_F12)
        return (uint8_t)(0x80 + (sym - SDLK_F1)); // F1=0x80, F2=0x81, ...
    switch (sym) {
        case SDLK_UP:       return 0x99;
        case SDLK_DOWN:     return 0x98;
        case SDLK_LEFT:     return 0x97;
        case SDLK_RIGHT:    return 0x96;
        case SDLK_DELETE:   return 0x93;
        case SDLK_ESCAPE:   return 0x1B;
        case SDLK_RETURN:   return '\r';
        case SDLK_TAB:      return '\t';
        case SDLK_BACKSPACE:return '\b';
        default:            return 0;
    }
}

// Map SDL modifier state to Esposito modifiers
// Left Alt = Fn, Right Alt/Ctrl = Fn2
static uint8_t sdl_to_mods(SDL_Keymod mod) {
    uint8_t m = 0;
    if (mod & KMOD_LSHIFT) m |= MODIFIER_SHIFT;
    if (mod & KMOD_RSHIFT) m |= MODIFIER_SHIFT;
    if (mod & KMOD_LCTRL)  m |= MODIFIER_CTRL;
    if (mod & KMOD_RCTRL)  m |= MODIFIER_CTRL;
    if (mod & KMOD_LALT)   m |= MODIFIER_FN;   // Left Alt = Fn
    if (mod & KMOD_RALT)   m |= MODIFIER_FN2;  // Right Alt = Fn2
    return m;
}

static void flush_display(void) {
    // Sprite-based rendering (e.g. gameboy): queue the pending sprite
    // from the background display task and render it on the main thread
    if (sprite_get_active()) {
        sprite_render_pending();
        return;
    }
    if (graphics_mode_is_active())
        graphics_flush();
    else
        text_mode_flush();
}

int main(int argc, char *argv[]) {
    // Determine app name for config binding
    const char *app_name = "hello_world";
    if (argc > 1) app_name = argv[1];

    // Ensure emulated SD card mount point exists
    mkdir("/sdcard", 0755);

    // Initialize text mode (SDL2)
    if (!text_mode_init()) {
        fprintf(stderr, "Failed to initialize text mode\n");
        return 1;
    }
    SDL_SetWindowTitle(text_mode_get_window(), app_name);

    // Bind config for this app
    config_bind_app(app_name);

    // Create app context
    app_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.name, sizeof(ctx.name), "%s", app_name);
    ctx.init = app_init;
    ctx.event_fn = app_event;
    ctx.checkpoint = app_checkpoint;
    ctx.close = app_close;

    // Initialize app
    ctx.init(&ctx);

    // Event loop
    bool running = true;
    Uint32 timer_tick = 0;

    // Initial render
    flush_display();

    while (running) {
        SDL_Event sdl_event;
        while (SDL_PollEvent(&sdl_event)) {
            switch (sdl_event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    // Handle Ctrl+Q or Ctrl+ESC as quit
                    if (sdl_event.type == SDL_KEYDOWN) {
                        SDL_Keymod mod = SDL_GetModState();
                        if ((sdl_event.key.keysym.sym == SDLK_q && (mod & KMOD_LCTRL)) ||
                            (sdl_event.key.keysym.sym == SDLK_ESCAPE && (mod & KMOD_LCTRL))) {
                            running = false;
                            break;
                        }
                    }

                    // Build event
                    event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = EVENT_KEYBOARD;
                    ev.keyboard.pressed = (sdl_event.type == SDL_KEYDOWN);
                    ev.keyboard.key = sdl_key_to_ascii(sdl_event.key.keysym.sym, SDL_GetModState());
                    ev.keyboard.modifiers = sdl_to_mods(SDL_GetModState());
                    ev.keyboard.raw_key_code = sdl_to_raw_key(sdl_event.key.keysym.sym);

                    ctx.event_fn(&ctx, &ev);
                    flush_display();
                    break;
                }

                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP: {
                    event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = EVENT_TOUCH;
                    ev.touch.x = sdl_event.button.x;
                    ev.touch.y = sdl_event.button.y;
                    ev.touch.pressed = (sdl_event.type == SDL_MOUSEBUTTONDOWN);
                    ctx.event_fn(&ctx, &ev);
                    flush_display();
                    break;
                }
            }
        }

        // Timer events
        if (ctx.timer_interval_ms > 0) {
            Uint32 now = SDL_GetTicks();
            if (now - timer_tick >= ctx.timer_interval_ms) {
                event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = EVENT_TIMER;
                ctx.event_fn(&ctx, &ev);
                flush_display();
                timer_tick = now;
            }
        }

        // Cap frame rate
        SDL_Delay(16); // ~60 fps
    }

    // Cleanup
    ctx.close(&ctx);
    SDL_Quit();
    return 0;
}
