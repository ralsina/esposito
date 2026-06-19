#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"
#include "ui2.h"
#include "lucide_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PET_NAME "Osito"

typedef enum {
    STAGE_EGG = 0, STAGE_BABY, STAGE_CHILD, STAGE_TEEN, STAGE_ADULT, STAGE_DEAD
} stage_t;

typedef enum {
    MOOD_HAPPY, MOOD_NEUTRAL, MOOD_SAD, MOOD_SLEEPING, MOOD_SICK, MOOD_DEAD, MOOD_EGG
} mood_t;

typedef struct {
    int birth;
    int last_update;
    int last_poop;
    int hunger;
    int happy;
    int energy;
    int clean;
    int health;
    bool sleeping;
    bool sick;
    bool has_poop;
    bool alive;
} pet_state_t;

static pet_state_t pet;
static ui2_screen_t *screen = NULL;
static char status_msg[48] = "";
static int64_t status_until;    // mono-ms until which status_msg shows (0 = none)
static int anim_frame = 0;     // for blink/bob animation

// Forward declarations (defined in the time section below).
static int trusted_now(void);
static int64_t mono_ms(void);
int64_t esp_timer_get_time(void);   // provided by firmware (symbol table)

// --- config persistence ---

static void load_state(void) {
    pet.birth       = config_get_int("birth", 0);
    pet.last_update = config_get_int("upd", 0);
    pet.last_poop   = config_get_int("lpoop", 0);
    pet.hunger      = config_get_int("hunger", 100);
    pet.happy       = config_get_int("happy", 100);
    pet.energy      = config_get_int("energy", 100);
    pet.clean       = config_get_int("clean", 100);
    pet.health      = config_get_int("health", 100);
    pet.sleeping    = config_get_bool("sleep", false);
    pet.sick        = config_get_bool("sick", false);
    pet.has_poop    = config_get_bool("poop", false);
    pet.alive       = config_get_bool("alive", true);

    // First launch: a fresh egg. Only stamp the birth time if the clock is
    // synced; otherwise leave birth=0 and it gets set on the first synced boot.
    if (pet.birth == 0) {
        int now = trusted_now();
        if (now > 0) {
            pet.birth = now;
            pet.last_update = now;
            pet.last_poop = now;
        }
    }
}

static void save_state(void) {
    config_set_int("birth", pet.birth);
    config_set_int("upd", pet.last_update);
    config_set_int("lpoop", pet.last_poop);
    config_set_int("hunger", pet.hunger);
    config_set_int("happy", pet.happy);
    config_set_int("energy", pet.energy);
    config_set_int("clean", pet.clean);
    config_set_int("health", pet.health);
    config_set_bool("sleep", pet.sleeping);
    config_set_bool("sick", pet.sick);
    config_set_bool("poop", pet.has_poop);
    config_set_bool("alive", pet.alive);
}

// --- time, decay, stages ---

static int clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

// Wall-clock seconds, but ONLY if NTP has synced. Returns 0 when the clock
// is untrusted (no RTC, so the clock is garbage until WiFi+NTP succeed).
// Every persistence/decay decision goes through this: a timestamp is only
// meaningful if the clock was synced both when stored and when read.
static int trusted_now(void) {
    os_time_status_t ts;
    if (os_get_time_status(&ts) && ts.synchronized) return (int)ts.unix_time;
    return 0;
}

// Monotonic milliseconds since boot — always available, used for short-lived
// UI timeouts that must work even when the wall clock is unsynced.
static int64_t mono_ms(void) {
    return esp_timer_get_time() / 1000;
}

static stage_t get_stage(void) {
    if (!pet.alive) return STAGE_DEAD;
    if (pet.birth == 0) return STAGE_EGG;          // never born (clock unsynced at first launch)
    int now = trusted_now();
    if (now == 0) return STAGE_EGG;                 // clock unsynced: can't know age, show egg (decay is frozen anyway)
    int age = now - pet.birth;
    if (age < 0)        return STAGE_EGG;           // clock jumped back; treat as newborn-safe
    if (age < 60)       return STAGE_EGG;           // 1 minute as an egg
    else if (age < 600) return STAGE_BABY;          // 10 minutes
    else if (age < 3600) return STAGE_CHILD;        // 1 hour
    else if (age < 86400) return STAGE_TEEN;        // 1 day
    else                 return STAGE_ADULT;
}

static mood_t get_mood(stage_t stage) {
    if (stage == STAGE_DEAD) return MOOD_DEAD;
    if (stage == STAGE_EGG)  return MOOD_EGG;
    if (pet.sleeping) return MOOD_SLEEPING;
    if (pet.sick)     return MOOD_SICK;
    int avg = (pet.hunger + pet.happy + pet.energy) / 3;
    if (avg >= 66) return MOOD_HAPPY;
    if (avg >= 33) return MOOD_NEUTRAL;
    return MOOD_SAD;
}

static const char *stage_name(stage_t s) {
    switch (s) {
        case STAGE_EGG:   return "Egg";
        case STAGE_BABY:  return "Baby";
        case STAGE_CHILD: return "Child";
        case STAGE_TEEN:  return "Teen";
        case STAGE_ADULT: return "Adult";
        default:          return "Ghost";
    }
}

// Apply real-time decay since last_update. Frozen entirely while the clock
// is unsynced — without an RTC we cannot trust elapsed time, so we refuse to
// decay (and never kill the pet) until NTP gives us a reliable clock.
static void apply_decay(void) {
    int now = trusted_now();
    if (now == 0) return;

    stage_t stage = get_stage();
    if (stage == STAGE_EGG || stage == STAGE_DEAD) {
        pet.last_update = now;
        return;
    }

    // No trusted timestamp on record yet (e.g. first synced boot after the
    // pet was created unsynced): seed it now, don't invent decay.
    if (pet.last_update == 0) {
        pet.last_update = now;
        pet.last_poop = now;
        return;
    }

    int elapsed = now - pet.last_update;
    if (elapsed <= 0) { pet.last_update = now; return; }  // clock jumped back
    if (elapsed > 72 * 3600) elapsed = 72 * 3600;          // cap: 3 days
    double hours = elapsed / 3600.0;

    pet.hunger -= (int)(10 * hours);
    pet.happy  -= (int)(8 * hours);
    pet.clean  -= (int)(6 * hours);
    if (pet.sleeping) pet.energy += (int)(25 * hours);
    else              pet.energy -= (int)(5 * hours);

    // Poop accumulates roughly every 4 hours.
    if (now - pet.last_poop > 4 * 3600) {
        pet.has_poop = true;
        pet.last_poop = now;
        pet.clean -= 15;
    }

    pet.hunger = clamp100(pet.hunger);
    pet.happy  = clamp100(pet.happy);
    pet.energy = clamp100(pet.energy);
    pet.clean  = clamp100(pet.clean);

    // Health: drops when a need is critically unmet; recovers when comfortable.
    int critical = 0;
    if (pet.hunger <= 0) critical++;
    if (pet.happy <= 0)  critical++;
    if (pet.clean <= 0)  critical++;
    if (pet.energy <= 0) critical++;
    if (pet.sick)        critical++;
    if (critical > 0) {
        pet.health -= (int)(12 * critical * hours);
    } else {
        pet.health += (int)(5 * hours);
    }

    // Sickness from filth or starvation.
    if (!pet.sick && (pet.clean <= 0 || pet.hunger <= 0)) {
        pet.sick = true;
    }

    pet.health = clamp100(pet.health);
    if (pet.health <= 0) {
        pet.health = 0;
        pet.alive = false;
        pet.sleeping = false;
    }

    // Wake up when fully rested.
    if (pet.sleeping && pet.energy >= 100) {
        pet.sleeping = false;
    }

    pet.last_update = now;
}

static void set_status(const char *msg) {
    snprintf(status_msg, sizeof(status_msg), "%s", msg);
    status_until = mono_ms() + 3000;   // monotonic: works even with unsynced clock
}

// --- actions ---

static void act_feed(void) {
    stage_t s = get_stage();
    if (s == STAGE_EGG)  { set_status("The egg just wobbles."); return; }
    if (s == STAGE_DEAD) { return; }
    if (pet.sleeping)    { set_status("Shhh... it's sleeping."); return; }
    if (pet.hunger >= 95) { set_status("It's not hungry."); return; }
    pet.hunger = clamp100(pet.hunger + 30);
    pet.clean  = clamp100(pet.clean - 4);
    set_status("Yum! *chomp chomp*");
}

static void act_play(void) {
    stage_t s = get_stage();
    if (s == STAGE_EGG)  { set_status("You can't play with an egg."); return; }
    if (s == STAGE_DEAD) { return; }
    if (pet.sleeping)    { set_status("Shhh... it's sleeping."); return; }
    if (pet.energy <= 10) { set_status("It's too tired to play."); return; }
    pet.happy  = clamp100(pet.happy + 25);
    pet.energy = clamp100(pet.energy - 15);
    pet.hunger = clamp100(pet.hunger - 8);
    set_status("Wheee! So much fun!");
}

static void act_sleep(void) {
    stage_t s = get_stage();
    if (s == STAGE_EGG)  { set_status("The egg can't sleep."); return; }
    if (s == STAGE_DEAD) { return; }
    pet.sleeping = !pet.sleeping;
    set_status(pet.sleeping ? "Zzz... goodnight." : "Good morning!");
}

static void act_clean(void) {
    stage_t s = get_stage();
    if (s == STAGE_EGG)  { set_status("The egg doesn't need cleaning."); return; }
    if (s == STAGE_DEAD) { return; }
    if (pet.clean >= 95 && !pet.has_poop) { set_status("It's already spotless."); return; }
    pet.clean = clamp100(pet.clean + 50);
    pet.has_poop = false;
    set_status("Sparkly clean! *splash*");
}

static void act_meds(void) {
    stage_t s = get_stage();
    if (s == STAGE_EGG)  { set_status("The egg is fine."); return; }
    if (s == STAGE_DEAD) { return; }
    if (!pet.sick) { set_status("It's not sick."); return; }
    pet.sick = false;
    pet.health = clamp100(pet.health + 20);
    set_status("Feeling better already!");
}

static void act_rebirth(void) {
    // Bring the pet back as a fresh egg.
    int now = trusted_now();
    pet.birth = now;           // 0 if unsynced -> waits as egg until synced
    pet.last_update = now;
    pet.last_poop = now;
    pet.hunger = pet.happy = pet.energy = pet.clean = pet.health = 100;
    pet.sleeping = pet.sick = pet.has_poop = false;
    pet.alive = true;
    set_status("A new egg appears!");
}

// --- rendering ---

static void print_centered(int row, const char *str, uint8_t color, uint8_t attr) {
    int cols = text_mode_get_cols();
    int x = (cols - (int)strlen(str)) / 2;
    if (x < 0) x = 0;
    text_mode_print_at_attr(x, row, str, color, attr);
}

static void draw_creature(int top_row) {
    stage_t stage = get_stage();
    mood_t mood = get_mood(stage);

    // Eyes (2 chars) and mouth (1 char) per mood.
    const char *eyes, *mouth;
    switch (mood) {
        case MOOD_HAPPY:     eyes = "^ ^"; mouth = "w"; break;
        case MOOD_NEUTRAL:   eyes = "- -"; mouth = "-"; break;
        case MOOD_SAD:       eyes = "T T"; mouth = "_"; break;
        case MOOD_SLEEPING:  eyes = "- -"; mouth = "o"; break;
        case MOOD_SICK:      eyes = "x x"; mouth = "~"; break;
        case MOOD_DEAD:      eyes = "X X"; mouth = "x"; break;
        default:             eyes = ". ."; mouth = "."; break;  // egg (unused)
    }

    int blink = (anim_frame % 8 == 0) && mood != MOOD_DEAD && mood != MOOD_SLEEPING;
    if (blink) { eyes = "- -"; }

    char l1[24], l2[24], l3[24], l4[24], l5[24];

    switch (stage) {
        case STAGE_EGG: {
            int wob = anim_frame % 2;
            snprintf(l1, sizeof(l1), "%*s ,-=-.", wob, "");
            snprintf(l2, sizeof(l2), "%*s(     )", wob, "");
            snprintf(l3, sizeof(l3), "%*s `---'", wob, "");
            print_centered(top_row,     l1, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            print_centered(top_row + 1, l2, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            print_centered(top_row + 2, l3, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            return;
        }
        case STAGE_BABY:
            snprintf(l1, sizeof(l1), " .---.");
            snprintf(l2, sizeof(l2), "(%s)", eyes);
            snprintf(l3, sizeof(l3), " ( %s )", mouth);
            snprintf(l4, sizeof(l4), "  `-'");
            print_centered(top_row,     l1, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_BOLD);
            print_centered(top_row + 1, l2, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_BOLD);
            print_centered(top_row + 2, l3, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_BOLD);
            print_centered(top_row + 3, l4, TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_BOLD);
            return;
        case STAGE_CHILD:
            snprintf(l1, sizeof(l1), "  .---.");
            snprintf(l2, sizeof(l2), " (%s)", eyes);
            snprintf(l3, sizeof(l3), " | %s |", mouth);
            snprintf(l4, sizeof(l4), "  `---'");
            print_centered(top_row,     l1, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
            print_centered(top_row + 1, l2, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
            print_centered(top_row + 2, l3, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
            print_centered(top_row + 3, l4, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
            return;
        case STAGE_TEEN:
            snprintf(l1, sizeof(l1), "   .-----.");
            snprintf(l2, sizeof(l2), "  (%s)", eyes);
            snprintf(l3, sizeof(l3), "  |  %s  |", mouth);
            snprintf(l4, sizeof(l4), "   `-----'");
            snprintf(l5, sizeof(l5), "    | |");
            print_centered(top_row,     l1, TEXT_COLOR_BRIGHT_MAGENTA, TEXT_ATTR_BOLD);
            print_centered(top_row + 1, l2, TEXT_COLOR_BRIGHT_MAGENTA, TEXT_ATTR_BOLD);
            print_centered(top_row + 2, l3, TEXT_COLOR_BRIGHT_MAGENTA, TEXT_ATTR_BOLD);
            print_centered(top_row + 3, l4, TEXT_COLOR_BRIGHT_MAGENTA, TEXT_ATTR_BOLD);
            print_centered(top_row + 4, l5, TEXT_COLOR_BRIGHT_MAGENTA, TEXT_ATTR_BOLD);
            return;
        case STAGE_ADULT:
            snprintf(l1, sizeof(l1), "    .-------.");
            snprintf(l2, sizeof(l2), "   ( %s )", eyes);
            snprintf(l3, sizeof(l3), "   |   %s   |", mouth);
            snprintf(l4, sizeof(l4), "  /         \\");
            snprintf(l5, sizeof(l5), "  `---------'  | | |");
            print_centered(top_row,     l1, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            print_centered(top_row + 1, l2, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            print_centered(top_row + 2, l3, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            print_centered(top_row + 3, l4, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            print_centered(top_row + 4, l5, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
            return;
        case STAGE_DEAD:
            print_centered(top_row,     "  .-.",   TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
            print_centered(top_row + 1, " (X X)",  TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
            print_centered(top_row + 2, "  |-|",   TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
            print_centered(top_row + 3, "  `v'",   TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
            print_centered(top_row + 4, "R.I.P. " PET_NAME, TEXT_COLOR_BRIGHT_RED, TEXT_ATTR_BOLD);
            return;
    }
}

static void draw_stat(int row, const char *label, int value) {
    int cols = text_mode_get_cols();
    char bar[40];
    int bar_max = cols - 14;
    if (bar_max > 30) bar_max = 30;
    if (bar_max < 8) bar_max = 8;
    int filled = (value * bar_max + 50) / 100;

    uint8_t color;
    if (value >= 50)      color = TEXT_COLOR_BRIGHT_GREEN;
    else if (value >= 25) color = TEXT_COLOR_YELLOW;
    else                  color = TEXT_COLOR_BRIGHT_RED;

    int pos = 0;
    pos += snprintf(bar + pos, sizeof(bar) - pos, "%-7s [", label);
    for (int i = 0; i < bar_max; i++) {
        bar[pos++] = (i < filled) ? '#' : '.';
    }
    bar[pos++] = ']';
    bar[pos++] = '\0';
    text_mode_print_at_attr(2, row, bar, color, TEXT_ATTR_NORMAL);
}

static void draw_header(void) {
    stage_t stage = get_stage();
    char hdr[80];
    int now = trusted_now();
    if (pet.birth != 0 && now != 0) {
        int age = now - pet.birth;
        if (age < 0) age = 0;
        int days = age / 86400;
        int hours = (age % 86400) / 3600;
        int mins = (age % 3600) / 60;
        if (days > 0)
            snprintf(hdr, sizeof(hdr), "%s the %s   age %dd %dh", PET_NAME, stage_name(stage), days, hours);
        else
            snprintf(hdr, sizeof(hdr), "%s the %s   age %dh %dm", PET_NAME, stage_name(stage), hours, mins);
    } else {
        // Clock unsynced (or pet not yet born): show name + stage without age.
        snprintf(hdr, sizeof(hdr), "%s the %s", PET_NAME, stage_name(stage));
    }
    text_mode_print_at_attr(0, 0, hdr, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
}

static void draw_status(int row) {
    if (status_until > 0 && mono_ms() < status_until && status_msg[0]) {
        print_centered(row, status_msg, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_NORMAL);
        return;
    }
    // Warn when the clock isn't synced: time-based decay is paused.
    if (trusted_now() == 0) {
        print_centered(row, "(clock not synced — time paused until NTP)", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
        return;
    }
    if (pet.has_poop && get_stage() != STAGE_EGG) {
        print_centered(row, "(there's a mess to clean!)", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
        return;
    }

    stage_t s = get_stage();
    mood_t m = get_mood(s);
    const char *line = "";
    if (s == STAGE_EGG)       line = "(the egg is warm... waiting)";
    else if (s == STAGE_DEAD) line = "Press [R] or New for a new egg";
    else switch (m) {
        case MOOD_SLEEPING: line = "Zzz..."; break;
        case MOOD_SICK:     line = "*cough* I don't feel well..."; break;
        case MOOD_SAD:      line = "I'm sad... pay attention to me!"; break;
        case MOOD_NEUTRAL:  line = "I'm okay."; break;
        case MOOD_HAPPY:    line = "I love you!"; break;
        default: break;
    }
    print_centered(row, line, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
}

static void draw_stats(int top_row) {
    draw_stat(top_row,     "Hunger", pet.hunger);
    draw_stat(top_row + 1, "Happy",  pet.happy);
    draw_stat(top_row + 2, "Energy", pet.energy);
    draw_stat(top_row + 3, "Clean",  pet.clean);
    draw_stat(top_row + 4, "Health", pet.health);
}

static void render(void) {
    apply_decay();

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    text_mode_clear(TEXT_COLOR_BLACK);

    draw_header();

    int btn_rows = 3;
    int creature_top = 2;
    int status_row = creature_top + 6;
    int stats_top = status_row + 2;

    draw_creature(creature_top);
    draw_status(status_row);
    draw_stats(stats_top);

    if (screen) {
        ui2_screen_render(screen);
    }

    // Action hint line, just under stats, above buttons.
    int hint_row = rows - btn_rows - 1;
    if (hint_row > stats_top + 4) {
        char hint[64];
        if (get_stage() == STAGE_DEAD)
            snprintf(hint, sizeof(hint), "[R] New egg   %*sCtrl+Esc Exit", cols - 26, "");
        else
            snprintf(hint, sizeof(hint), "F Feed  P Play  S Sleep  C Clean  M Meds");
        text_mode_print_at_attr(0, hint_row, hint, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    }

    text_mode_flush();
}

// --- buttons ---

static void on_feed(ui2_button_t *b, void *d)   { (void)b; (void)d; act_feed(); render(); }
static void on_play(ui2_button_t *b, void *d)   { (void)b; (void)d; act_play(); render(); }
static void on_sleep(ui2_button_t *b, void *d)  { (void)b; (void)d; act_sleep(); render(); }
static void on_clean(ui2_button_t *b, void *d)  { (void)b; (void)d; act_clean(); render(); }
static void on_meds(ui2_button_t *b, void *d)   { (void)b; (void)d; act_meds(); render(); }
static void on_rebirth(ui2_button_t *b, void *d){ (void)b; (void)d; act_rebirth(); render(); }
static void on_quit(ui2_button_t *b, void *d)    { (void)b; (void)d; os_load_app("launcher"); }

static void build_screen(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    screen = ui2_screen_create();
    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_screen_set_root(screen, root);

    int btn_row = rows - 3;
    ui2_layout_t *bar = ui2_layout_create(0, btn_row, cols, 3, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(bar, 1);
    ui2_layout_add(root, UI2_WIDGET(bar));

    stage_t stage = get_stage();
    struct { const char *label; void (*cb)(ui2_button_t *, void *); } btns[6];
    int n = 0;
    if (stage == STAGE_DEAD) {
        btns[n++] = (typeof(btns[0])){"New", on_rebirth};
        btns[n++] = (typeof(btns[0])){ICON_X, on_quit};
    } else {
        btns[n++] = (typeof(btns[0])){"Feed",  on_feed};
        btns[n++] = (typeof(btns[0])){"Play",  on_play};
        btns[n++] = (typeof(btns[0])){"Sleep", on_sleep};
        btns[n++] = (typeof(btns[0])){"Clean", on_clean};
        btns[n++] = (typeof(btns[0])){"Meds",  on_meds};
        btns[n++] = (typeof(btns[0])){ICON_X, on_quit};
    }

    int btn_w = (cols - (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        ui2_button_t *b = ui2_button_create(0, 0, btn_w, 3, btns[i].label);
        ui2_button_set_colors(b, TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLUE);
        ui2_button_set_callback(b, btns[i].cb, NULL);
        ui2_layout_add(bar, UI2_WIDGET(b));
    }
}

// --- app lifecycle ---

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH | EVENT_TIMER;
    ctx->timer_interval_ms = 1000;

    text_mode_init();
    load_state();
    build_screen();
    render();
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;

    if (event->type == EVENT_TIMER) {
        anim_frame++;
        render();
        return;
    }

    if (event->type == EVENT_TOUCH) {
        if (ui2_screen_handle_event(screen, event)) {
            render();
        }
        return;
    }

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        stage_t stage = get_stage();
        if (stage == STAGE_DEAD) {
            if (key == 'r' || key == 'R') { act_rebirth(); }
        } else {
            switch (key) {
                case 'f': case 'F': act_feed(); break;
                case 'p': case 'P': act_play(); break;
                case 's': case 'S': act_sleep(); break;
                case 'c': case 'C': act_clean(); break;
                case 'm': case 'M': act_meds(); break;
                default: break;
            }
        }
        render();
    }
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
    save_state();
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    save_state();
    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
    }
    text_mode_clear(TEXT_COLOR_BLACK);
}
