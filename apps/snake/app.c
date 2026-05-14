// # Snakes and Ositos
//
// ![Snake game](snake.png)
//
// This is an implementation of the classic snake minigame
// for the ESPOsito operating system. I think it's a nice
// example of the basics of creating an app for this OS,
// and small enough to be understood in a few minutes
// if you know basic C. So, let's give this a shot!
//
// We start with some includes, as is traditional.
// Nothing very surprising, other than these three
// which are provided by the OS.

#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Some constants for the game

#define HUD_ROW_SCORE 0
#define HUD_ROW_HELP 1
#define PLAY_TOP 2

#define MAX_SNAKE_CELLS 2048
#define TIMER_BASE_MS 170
#define TIMER_STEP_MS 10
#define TIMER_MIN_MS 80
#define RAMP_EVERY_FOOD 3

// The snake is an array of cells, true of this snake
// and of the more traditional biological snakes.

typedef struct {
    int x;
    int y;
} cell_t;

static cell_t snake[MAX_SNAKE_CELLS];
static int snake_len = 0;

// Some variables for the game state

static int dir_x = 1;
static int dir_y = 0;
static int next_dir_x = 1;
static int next_dir_y = 0;
static int food_x = 0;
static int food_y = 0;
static int score = 0;
static int high_score = 0;
static int running = 0;
static int game_over = 0;
static uint32_t prng_state = 0x12345678u;
static int cols = 0;
static int rows = 0;
static int speed_ms = TIMER_BASE_MS;
static app_context_t *g_ctx = NULL;

// A rectangle.

typedef struct {
    int x0;
    int x1;
    int y0;
    int y1;
} button_rect_t;

// Touch regions for invisible directional controls.
// We divide the screen into quadrants: top/bottom halves and left/right sides.

static button_rect_t touch_up;
static button_rect_t touch_down;
static button_rect_t touch_left;
static button_rect_t touch_right;

// Get a random number

static uint32_t next_random(void) {
    prng_state = prng_state * 1664525u + 1013904223u;
    return prng_state;
}

// Is this position in the playfield? Useful to know if the
// player crashed into the walls.

static int inside_playfield(int x, int y) {
    if (x < 0 || x >= cols) return 0;
    if (y < PLAY_TOP || y >= rows) return 0;
    return 1;
}

// Is this position in this rectangle?

static int touch_in_button(const button_rect_t *button, int x, int y) {
    return x >= button->x0 && x <= button->x1 && y >= button->y0 && y <= button->y1;
}

// Bump speed if needed, by which we mean make the delays lower.

static void update_speed(void) {
    int level = score / RAMP_EVERY_FOOD;
    int next_speed = TIMER_BASE_MS - (level * TIMER_STEP_MS);
    if (next_speed < TIMER_MIN_MS) {
        next_speed = TIMER_MIN_MS;
    }
    speed_ms = next_speed;
    if (g_ctx) {
        g_ctx->timer_interval_ms = (uint32_t)speed_ms;
    }
}

// Is this position in the snake?
// Useful to know if we crashed into the snake.

static int snake_contains(int x, int y) {
    for (int index = 0; index < snake_len; index++) {
        if (snake[index].x == x && snake[index].y == y) {
            return 1;
        }
    }
    return 0;
}

// Maybe HUD is a bit pretentious but this shows the score and that
// sort of things. Notice the use of `text_mode_print_at_color`,
// that is one of the ways `text_mode` lets you draw text. There are
// a few similar functions like `text_mode_print_at_attr`.

static void draw_hud(void) {
    char line[80];
    snprintf(line, sizeof(line), "Snake  Score:%d  High:%d  Spd:%dms", score, high_score, speed_ms);

    for (int x = 0; x < cols; x++) {
        text_mode_print_at_color(x, HUD_ROW_SCORE, " ", TEXT_COLOR_BRIGHT_WHITE);
        text_mode_print_at_color(x, HUD_ROW_HELP, " ", TEXT_COLOR_CYAN);
    }

    text_mode_print_at_attr(0, HUD_ROW_SCORE, line, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);

    if (game_over) {
        text_mode_print_at_color(0, HUD_ROW_HELP, "Game over: ENTER restart, WASD or touch", TEXT_COLOR_YELLOW);
    } else if (!running) {
        text_mode_print_at_color(0, HUD_ROW_HELP, "Press WASD or touch to start", TEXT_COLOR_CYAN);
    } else {
        text_mode_print_at_color(0, HUD_ROW_HELP, "WASD/touch move, Ctrl+ESC launcher", TEXT_COLOR_CYAN);
    }
}

// Update the invisible touch regions based on screen size.
// The screen is divided into 4 regions:
// - Top half (above middle): UP direction
// - Bottom half (below middle): DOWN direction
// - Left third: LEFT direction
// - Right third: RIGHT direction

static void setup_touch_regions(void) {
    int middle_y = PLAY_TOP + (rows - PLAY_TOP) / 2;
    int third_x = cols / 3;

    touch_up.x0 = 0;
    touch_up.x1 = cols - 1;
    touch_up.y0 = PLAY_TOP;
    touch_up.y1 = middle_y - 1;

    touch_down.x0 = 0;
    touch_down.x1 = cols - 1;
    touch_down.y0 = middle_y;
    touch_down.y1 = rows - 1;

    touch_left.x0 = 0;
    touch_left.x1 = third_x;
    touch_left.y0 = PLAY_TOP;
    touch_left.y1 = rows - 1;

    touch_right.x0 = cols - third_x - 1;
    touch_right.x1 = cols - 1;
    touch_right.y0 = PLAY_TOP;
    touch_right.y1 = rows - 1;
}

// Draw the background.

static void draw_cell(int x, int y, const char *ch, uint8_t color) {
    if (inside_playfield(x, y)) {
        text_mode_print_at_color(x, y, ch, color);
    }
}

// Food for the snake! In a random place, not inside the snake :-)

static void spawn_food(void) {
    int play_rows = rows - PLAY_TOP;
    int cell_count = cols * play_rows;

    if (cell_count <= 0) {
        food_x = 0;
        food_y = PLAY_TOP;
        return;
    }

    int start = (int)(next_random() % (uint32_t)cell_count);
    for (int probe = 0; probe < cell_count; probe++) {
        int candidate = (start + probe) % cell_count;
        int x = candidate % cols;
        int y = PLAY_TOP + (candidate / cols);
        if (!snake_contains(x, y)) {
            food_x = x;
            food_y = y;
            draw_cell(food_x, food_y, "*", TEXT_COLOR_YELLOW);
            return;
        }
    }

    food_x = -1;
    food_y = -1;
}

// Clear

static void clear_playfield(void) {
    for (int y = PLAY_TOP; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_color(x, y, " ", TEXT_COLOR_BLACK);
        }
    }
}

// Draw the whole snake and nothing but the snake

static void draw_snake_full(void) {
    for (int index = 0; index < snake_len; index++) {
        draw_cell(snake[index].x, snake[index].y, index == 0 ? "@" : "o", index == 0 ? TEXT_COLOR_BRIGHT_GREEN : TEXT_COLOR_GREEN);
    }
}

// Reset everything!

static void reset_game(void) {
    cols = text_mode_get_cols();
    rows = text_mode_get_rows();

    if (cols <= 0 || rows <= PLAY_TOP + 2) {
        return;
    }

    score = 0;
    running = 0;
    game_over = 0;
    update_speed();
    dir_x = 1;
    dir_y = 0;
    next_dir_x = 1;
    next_dir_y = 0;

    int cx = cols / 2;
    int cy = PLAY_TOP + (rows - PLAY_TOP) / 2;

    snake_len = 4;
    for (int index = 0; index < snake_len; index++) {
        snake[index].x = cx - index;
        snake[index].y = cy;
    }

    clear_playfield();
    draw_hud();
    setup_touch_regions();
    draw_snake_full();
    spawn_food();
    text_mode_flush();
}

// Sets the direction of snake movement.

static void set_direction(int dx, int dy) {
    if ((dx == -dir_x && dy == -dir_y) || (dx == -next_dir_x && dy == -next_dir_y)) {
        return;
    }
    next_dir_x = dx;
    next_dir_y = dy;
    running = 1;
}

// We're done playing! Notice how we have a config system where we can save
// the high score for later.

static void finish_game(void) {
    game_over = 1;
    running = 0;
    if (score > high_score) {
        high_score = score;
        config_set_int("snake/high_score", high_score);
    }
    draw_hud();
    text_mode_flush();
}

// The actual game code. We call this repeatedly, and that
// is how the game progresses.

static void step_game(void) {
    if (!running || game_over || snake_len <= 0) {
        return;
    }

    dir_x = next_dir_x;
    dir_y = next_dir_y;

    int new_x = snake[0].x + dir_x;
    int new_y = snake[0].y + dir_y;

    if (!inside_playfield(new_x, new_y)) {
        finish_game();
        return;
    }

    int ate_food = (new_x == food_x && new_y == food_y);
    int tail_index = snake_len - 1;

    for (int index = 0; index < snake_len; index++) {
        if (snake[index].x == new_x && snake[index].y == new_y) {
            if (!(index == tail_index && !ate_food)) {
                finish_game();
                return;
            }
        }
    }

    cell_t previous_tail = snake[tail_index];

    if (ate_food) {
        if (snake_len < MAX_SNAKE_CELLS) {
            snake_len++;
        }
        score++;
        update_speed();
        draw_hud();
    }

    for (int index = snake_len - 1; index > 0; index--) {
        snake[index] = snake[index - 1];
    }

    snake[0].x = new_x;
    snake[0].y = new_y;

    draw_cell(new_x, new_y, "@", TEXT_COLOR_BRIGHT_GREEN);
    if (snake_len > 1) {
        draw_cell(snake[1].x, snake[1].y, "o", TEXT_COLOR_GREEN);
    }

    if (ate_food) {
        spawn_food();
    } else {
        draw_cell(previous_tail.x, previous_tail.y, " ", TEXT_COLOR_BLACK);
    }

    text_mode_flush();
}

// The `app_init` is what the operating system calls when the app starts.
// Think of it as a traditional C program's `main()`
//
// However, apps in this OS need to do some chores here.

void app_init(app_context_t *ctx) {

    // The app context is a place where the app communicates with the
    // OS.
    //
    g_ctx = ctx;
    update_speed();

    // For example, subscriptions means the OS will send those events
    // to the app. We want to know about keyboard, touch and timers.
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER | EVENT_TOUCH;
    // And the timer is set as the current speed of the game. This way
    // there is no "speed logic", things just happen faster!
    ctx->timer_interval_ms = (uint32_t)speed_ms;

    // This is a `text_mode` app, so init things.
    text_mode_init();
    text_mode_clear(TEXT_COLOR_BLACK);

    high_score = config_get_int("snake/high_score", 0);
    reset_game();
}

// Apps are required to implement this.
//
// Since this is NOT a multitasking OS, apps are expected to
// save enough state so that when they restart they will be
// in the state the user expects.
//
void app_checkpoint(app_context_t *ctx) {
    config_set_int("snake/high_score", high_score);
}

// Cleanup when the app closes.
void app_close(app_context_t *ctx) {
    g_ctx = NULL;
    text_mode_clear(TEXT_COLOR_BLACK);
}

// The event handler! This is called whenever one of the
// events we are subscribed to happens.

void app_event(app_context_t *ctx, event_t *event) {

    // Time has passed, move the snake, check collisions, etc.

    if (event->type == EVENT_TIMER) {
        step_game();
        return;
    }

    // User touched the screen! We check which region they touched
    // and interpret it as a direction input.

    if (event->type == EVENT_TOUCH && event->touch.pressed) {
        int char_width = text_mode_get_char_width();
        int char_height = text_mode_get_char_height();
        int tx = event->touch.x / char_width;
        int ty = event->touch.y / char_height;

        // Check which region was touched. Vertical regions (up/down)
        // take priority over horizontal regions (left/right).
        if (touch_in_button(&touch_up, tx, ty)) {
            set_direction(0, -1);
        } else if (touch_in_button(&touch_down, tx, ty)) {
            set_direction(0, 1);
            // Also allows restart by tapping anywhere in the lower region when game over
            if (game_over) {
                reset_game();
            }
        } else if (touch_in_button(&touch_left, tx, ty)) {
            set_direction(-1, 0);
        } else if (touch_in_button(&touch_right, tx, ty)) {
            set_direction(1, 0);
        }
        return;
    }

    // User *released* a key, we don't care.

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) {
        return;
    }

    // User pressed a key, we care!

    char key = event->keyboard.key;
    if ((event->keyboard.modifiers & MODIFIER_CTRL) && key >= 1 && key <= 26) {
        key = (char)('a' + key - 1);
    }

    // Enter or space when game over, we start a new game.

    if (game_over && (key == '\n' || key == '\r' || key == ' ')) {
        reset_game();
        return;
    }

    // Set the snake direction!

    if (key == 'w' || key == 'W') set_direction(0, -1);
    if (key == 's' || key == 'S') set_direction(0, 1);
    if (key == 'a' || key == 'A') set_direction(-1, 0);
    if (key == 'd' || key == 'D') set_direction(1, 0);
}

// And this is the tail of the snake and that is the whole thing!
