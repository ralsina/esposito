#include "spscr_p.h"
#include "spscr.h"

#include "spperif.h"
#include "z80.h"

#include <stdlib.h>
#include <string.h>

#include "graphics_mode.h"

int display_get_width(void);
int display_get_height(void);

int color_type = 0;

#define COLORNUM 16
struct rgb custom_colors[] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xD7}, {0xD7, 0x00, 0x00}, {0xD7, 0x00, 0xD7},
    {0x00, 0xD7, 0x00}, {0x00, 0xD7, 0xD7}, {0xD7, 0xD7, 0x00}, {0xD7, 0xD7, 0xD7},
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0x00}, {0xFF, 0x00, 0xFF},
    {0x00, 0xFF, 0x00}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0xFF},
};
struct rgb *spscr_crgb = custom_colors;

#define SCR_W 256
#define SCR_H 192

static int border_x = 32;
static int border_y = 24;

volatile int screen_visible = 1;
volatile int accept_keys = 1;

static int fb_pitch;
static uint16_t palette_rgb565[COLORNUM];

void spscr_init_colors(void)
{
    int w = display_get_width();
    int h = display_get_height();
    border_x = (w - SCR_W) / 2;
    border_y = (h - SCR_H) / 2;
    fb_pitch = w / 2 + (w & 1 ? 1 : 0);

    for (int i = 0; i < COLORNUM; i++) {
        int r = spscr_crgb[i].r * 255 / 255;
        int g = spscr_crgb[i].g * 255 / 255;
        int b = spscr_crgb[i].b * 255 / 255;
        palette_rgb565[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    graphics_set_palette(palette_rgb565, COLORNUM);
}

int my_lastborder = 100;

static void fill_border(int color)
{
    uint8_t *fb = graphics_mode_get_buffer();
    if (!fb) return;
    uint8_t bc = (uint8_t)(color * 0x11);
    int w = display_get_width();
    int h = display_get_height();
    int row_bytes = fb_pitch;

    for (int y = 0; y < border_y; y++)
        memset(fb + y * row_bytes, bc, row_bytes);
    for (int y = border_y + SCR_H; y < h; y++)
        memset(fb + y * row_bytes, bc, row_bytes);
    for (int y = border_y; y < border_y + SCR_H; y++) {
        uint8_t *row = fb + y * row_bytes;
        for (int x = 0; x < border_x; x += 2)
            row[x / 2] = bc;
        int right_start = border_x + SCR_W;
        for (int x = right_start; x < w; x += 2)
            row[x / 2] = bc;
    }
}

static int last_coli = -1;

byte *update_screen_line(byte *scrp, int coli, int scri, int border, qbyte *cmarkp)
{
    (void)scrp;
    (void)cmarkp;

    if (scri < 0 || coli < 0 || coli >= SCR_H) {
        last_coli = -1;
        return scrp;
    }

    if (coli != last_coli + 1) {
        if (border != my_lastborder) {
            fill_border(border);
            my_lastborder = border;
        }
    }
    last_coli = coli;

    if (border != my_lastborder) {
        fill_border(border);
        my_lastborder = border;
    }

    uint8_t *fb = graphics_mode_get_buffer();
    if (!fb) return scrp;

    int y = coli;
    int row_offset = (border_y + y) * fb_pitch + border_x / 2;
    int addr_base = 16384 + ((y & 0xC0) << 5) + ((y & 0x07) << 8) + ((y & 0x38) >> 3) * 32;
    int attr_base = 22528 + (y >> 3) * 32;
    int cached_ink = 0, cached_paper = 0;

    for (int x = 0; x < 256; x++) {
        int x8 = x >> 3;
        if ((x & 7) == 0) {
            byte attr = z80_proc.mem[attr_base + x8];
            int ink = attr & 0x07;
            int paper = (attr >> 3) & 0x07;
            int bright = (attr >> 6) & 1;
            if ((attr >> 7) & sp_flash_state) {
                int tmp = ink;
                ink = paper;
                paper = tmp;
            }
            cached_ink = ink | (bright << 3);
            cached_paper = paper | (bright << 3);
        }

        int pixel_byte = z80_proc.mem[addr_base + x8];
        uint8_t color_idx = ((pixel_byte >> (7 - (x & 7))) & 1) ? cached_ink : cached_paper;

        int fb_x = x;
        uint8_t *p = fb + row_offset + fb_x / 2;
        if ((fb_x & 1) == 0)
            *p = (*p & 0x0F) | (color_idx << 4);
        else
            *p = (*p & 0xF0) | color_idx;
    }

    return scrp;
}

void translate_screen(void)
{
    int border, scline;
    byte *scrptr;
    qbyte cmark = 0;

    scrptr = (byte *)sp_image;
    border = z80_proc.ula_outport & 0x07;

    if (border != sp_lastborder) {
        sp_border_update = 2;
        sp_lastborder = border;
    }

    for (scline = 0; scline < (TMNUM / 2); scline++)
        scrptr = update_screen_line(scrptr, sp_coli[scline], sp_scri[scline], border, &cmark);

    graphics_flush();
}

void flash_change(void)
{
    int i, j;
    byte *scp;
    qbyte *mcp;
    unsigned int val;

    mcp = sp_scr_mark + 0x2C0;
    scp = z80_proc.mem + 0x5800;

    for (i = 24; i > 0; i--) {
        mcp++;
        val = 0;
        for (j = 32; j > 0; j--) {
            scp++;
            val >>= 1;
            if (*scp & 0x80) val |= (1u << 31);
        }
        *mcp |= val;
    }
}

void spscr_init_line_pointers(int lines)
{
    int i, bs, y, scline;

    bs = (lines - 192) / 2;

    for (i = 0; i < PORT_TIME_NUM; i++) {
        sp_scri[i] = -2;

        if (i < ODDHF)
            scline = i;
        else
            scline = i - ODDHF;

        if (scline >= 64 - bs && scline < 256 + bs) {
            if (scline >= 64 && scline < 256) {
                y = scline - 64;
                sp_coli[i] = y;
                sp_scri[i] = 0x200 +
                    (y & 0xC0) + ((y & 0x07) << 3) + ((y & 0x38) >> 3);
            } else
                sp_scri[i] = -1;
        }
    }
}
