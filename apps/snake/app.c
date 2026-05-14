#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HUD_ROW_SCORE 0
#define HUD_ROW_HELP 1
#define PLAY_TOP 2

#define MAX_SNAKE_CELLS 2048
#define TIMER_MS 130

typedef struct {
    int x;
    int y;
} cell_t;

static cell_t snake[MAX_SNAKE_CELLS];
static int snake_len = 0;
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

static uint32_t next_random(void) {
    prng_state = prng_state * 1664525u + 1013904223u;
    return prng_state;
}

static int inside_playfield(int x, int y) {
    if (x < 0 || x >= cols) return 0;
    if (y < PLAY_TOP || y >= rows) return 0;
    return 1;
}

static int snake_contains(int x, int y) {
    for (int index = 0; index < snake_len; index++) {
        if (snake[index].x == x && snake[index].y == y) {
            return 1;
        }
    }
    return 0;
}

static void draw_hud(void) {
    char line[80];
    snprintf(line, sizeof(line), "Snake  Score:%d  High:%d", score, high_score);

    for (int x = 0; x < cols; x++) {
        text_mode_print_at_color(x, HUD_ROW_SCORE, " ", TEXT_COLOR_BRIGHT_WHITE);
        text_mode_print_at_color(x, HUD_ROW_HELP, " ", TEXT_COLOR_CYAN);
    }

    text_mode_print_at_attr(0, HUD_ROW_SCORE, line, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);

    if (game_over) {
        text_mode_print_at_color(0, HUD_ROW_HELP, "Game over: ENTER restart, WASD move", TEXT_COLOR_YELLOW);
    } else if (!running) {
        text_mode_print_at_color(0, HUD_ROW_HELP, "Press WASD to start", TEXT_COLOR_CYAN);
    } else {
        text_mode_print_at_color(0, HUD_ROW_HELP, "WASD move, Ctrl+ESC launcher", TEXT_COLOR_CYAN);
    }
}

static void draw_cell(int x, int y, const char *ch, uint8_t color) {
    if (inside_playfield(x, y)) {
        text_mode_print_at_color(x, y, ch, color);
    }
}

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

static void clear_playfield(void) {
    for (int y = PLAY_TOP; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            text_mode_print_at_color(x, y, " ", TEXT_COLOR_BLACK);
        }
    }
}

static void draw_snake_full(void) {
    for (int index = 0; index < snake_len; index++) {
        draw_cell(snake[index].x, snake[index].y, index == 0 ? "@" : "o", index == 0 ? TEXT_COLOR_BRIGHT_GREEN : TEXT_COLOR_GREEN);
    }
}

static void reset_game(void) {
    cols = text_mode_get_cols();
    rows = text_mode_get_rows();

    if (cols <= 0 || rows <= PLAY_TOP + 2) {
        return;
    }

    score = 0;
    running = 0;
    game_over = 0;
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
    draw_snake_full();
    spawn_food();
    text_mode_flush();
}

static void set_direction(int dx, int dy) {
    if ((dx == -dir_x && dy == -dir_y) || (dx == -next_dir_x && dy == -next_dir_y)) {
        return;
    }
    next_dir_x = dx;
    next_dir_y = dy;
    running = 1;
}

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

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TIMER;
    ctx->timer_interval_ms = TIMER_MS;

    text_mode_init();
    text_mode_clear(TEXT_COLOR_BLACK);

    high_score = config_get_int("snake/high_score", 0);
    reset_game();
}

void app_checkpoint(app_context_t *ctx) {
    config_set_int("snake/high_score", high_score);
}

void app_close(app_context_t *ctx) {
    text_mode_clear(TEXT_COLOR_BLACK);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_TIMER) {
        step_game();
        return;
    }

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) {
        return;
    }

    char key = event->keyboard.key;
    if ((event->keyboard.modifiers & MODIFIER_CTRL) && key >= 1 && key <= 26) {
        key = (char)('a' + key - 1);
    }

    if (game_over && (key == '\n' || key == '\r' || key == ' ')) {
        reset_game();
        return;
    }

    if (key == 'w' || key == 'W') set_direction(0, -1);
    if (key == 's' || key == 'S') set_direction(0, 1);
    if (key == 'a' || key == 'A') set_direction(-1, 0);
    if (key == 'd' || key == 'D') set_direction(1, 0);
}
