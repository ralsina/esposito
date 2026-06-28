#ifndef GB_APU_H
#define GB_APU_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Game Boy APU register base (0xFF10-0xFF3F mapped to 0x00-0x2F)
#define APU_REG_BASE 0xFF10

static uint8_t apu_regs[0x30];
static uint8_t wave_ram[16];

// Duty patterns (4 patterns, 8 bits each)
static const uint8_t duty_table[4][8] = {
    {0,0,0,0,0,0,0,1},  // 12.5%
    {1,0,0,0,0,0,0,1},  // 25%
    {1,0,0,0,0,1,1,1},  // 50%
    {0,1,1,1,1,1,1,0},  // 75%
};

typedef struct {
    uint32_t timer;
    uint32_t period;
    int seq_pos;
    int env_vol;
    int env_timer;
    int sweep_freq;
    int sweep_timer;
    int sweep_enabled;
    int length;
    bool enabled;
    bool dac_enabled;
} square_ch_t;

typedef struct {
    uint32_t timer;
    uint32_t period;
    int pos;
    int length;
    bool enabled;
    bool dac_enabled;
} wave_ch_t;

typedef struct {
    uint32_t timer;
    uint32_t period;
    uint16_t lfsr;
    int env_vol;
    int env_timer;
    int length;
    bool enabled;
    bool dac_enabled;
} noise_ch_t;

static square_ch_t ch1, ch2;
static wave_ch_t ch3;
static noise_ch_t ch4;
static int frame_seq_timer = 0;
static int frame_seq_step = 0;

// Forward declarations
static int16_t mix_sample(void);

uint8_t audio_read(const uint32_t addr) {
    if (addr < APU_REG_BASE || addr >= APU_REG_BASE + 0x30) return 0xFF;
    uint8_t reg = apu_regs[addr - APU_REG_BASE];
    // NR52: sound status, only bit 7 writable
    if (addr == 0xFF26) {
        reg = (reg & 0x80) | 0x70 |
              (ch1.enabled ? 0x01 : 0) |
              (ch2.enabled ? 0x02 : 0) |
              (ch3.enabled ? 0x04 : 0) |
              (ch4.enabled ? 0x08 : 0);
    }
    return reg;
}

void audio_write(const uint32_t addr, const uint8_t val) {
    if (addr < APU_REG_BASE || addr >= APU_REG_BASE + 0x30) return;
    int idx = addr - APU_REG_BASE;

    // NR52 must have bit 7 set for most registers to be writable
    if (addr != 0xFF26 && !(apu_regs[0x16] & 0x80)) return;

    apu_regs[idx] = val;

    switch (addr) {
        case 0xFF10: // NR10 - Ch1 sweep
            break;
        case 0xFF11: // NR11 - Ch1 duty/length
            ch1.length = 64 - (val & 0x3F);
            break;
        case 0xFF12: // NR12 - Ch1 envelope
            ch1.dac_enabled = (val & 0xF8) != 0;
            ch1.env_vol = val >> 4;
            ch1.env_timer = val & 0x07;
            break;
        case 0xFF13: // NR13 - Ch1 freq lo
            break;
        case 0xFF14: // NR14 - Ch1 freq hi / trigger
            if (val & 0x80) {
                int freq = ((val & 0x07) << 8) | apu_regs[3];
                ch1.period = (2048 - freq) * 2;
                ch1.timer = ch1.period;
                ch1.seq_pos = 0;
                ch1.enabled = ch1.dac_enabled;
                ch1.env_vol = apu_regs[2] >> 4;
                ch1.env_timer = apu_regs[2] & 0x07;
                ch1.sweep_freq = ((val & 0x07) << 8) | apu_regs[3];
                ch1.sweep_timer = (apu_regs[0] >> 4) & 0x07;
                ch1.sweep_enabled = (apu_regs[0] & 0x70) != 0;
                if (ch1.length == 0) ch1.length = 64;
            }
            break;
        case 0xFF16: // NR21 - Ch2 duty/length
            ch2.length = 64 - (val & 0x3F);
            break;
        case 0xFF17: // NR22 - Ch2 envelope
            ch2.dac_enabled = (val & 0xF8) != 0;
            ch2.env_vol = val >> 4;
            ch2.env_timer = val & 0x07;
            break;
        case 0xFF18: // NR23 - Ch2 freq lo
            break;
        case 0xFF19: // NR24 - Ch2 freq hi / trigger
            if (val & 0x80) {
                int freq = ((val & 0x07) << 8) | apu_regs[8];
                ch2.period = (2048 - freq) * 2;
                ch2.timer = ch2.period;
                ch2.seq_pos = 0;
                ch2.enabled = ch2.dac_enabled;
                ch2.env_vol = apu_regs[7] >> 4;
                ch2.env_timer = apu_regs[7] & 0x07;
                if (ch2.length == 0) ch2.length = 64;
            }
            break;
        case 0xFF1A: // NR30 - Ch3 DAC enable
            ch3.dac_enabled = (val & 0x80) != 0;
            break;
        case 0xFF1B: // NR31 - Ch3 length
            ch3.length = 256 - val;
            break;
        case 0xFF1C: // NR32 - Ch3 volume
            break;
        case 0xFF1D: // NR33 - Ch3 freq lo
            break;
        case 0xFF1E: // NR34 - Ch3 freq hi / trigger
            if (val & 0x80) {
                int freq = ((val & 0x07) << 8) | apu_regs[0x0D];
                ch3.period = (2048 - freq);
                ch3.timer = ch3.period;
                ch3.pos = 0;
                ch3.enabled = ch3.dac_enabled;
                if (ch3.length == 0) ch3.length = 256;
            }
            break;
        case 0xFF20: // NR41 - Ch4 length
            ch4.length = 64 - (val & 0x3F);
            break;
        case 0xFF21: // NR42 - Ch4 envelope
            ch4.dac_enabled = (val & 0xF8) != 0;
            ch4.env_vol = val >> 4;
            ch4.env_timer = val & 0x07;
            break;
        case 0xFF22: // NR43 - Ch4 frequency
            break;
        case 0xFF23: // NR44 - Ch4 trigger
            if (val & 0x80) {
                int div = apu_regs[0x12] & 0x07;
                int shift = apu_regs[0x12] >> 4;
                ch4.period = (div > 0 ? div * 16 : 8) << shift;
                ch4.timer = ch4.period;
                ch4.lfsr = 0x7FFF;
                ch4.enabled = ch4.dac_enabled;
                ch4.env_vol = apu_regs[0x11] >> 4;
                ch4.env_timer = apu_regs[0x11] & 0x07;
                if (ch4.length == 0) ch4.length = 64;
            }
            break;
        default:
            if (addr >= 0xFF30 && addr <= 0xFF3F)
                wave_ram[addr - 0xFF30] = val;
            break;
    }
}

// Frame sequencer: runs at 512 Hz, cycles through 8 steps
static void apu_frame_seq(void) {
    frame_seq_step = (frame_seq_step + 1) & 7;

    if (frame_seq_step & 1) {
        // Clock length
        if (ch1.enabled && ch1.length > 0 && --ch1.length == 0) ch1.enabled = false;
        if (ch2.enabled && ch2.length > 0 && --ch2.length == 0) ch2.enabled = false;
        if (ch3.enabled && ch3.length > 0 && --ch3.length == 0) ch3.enabled = false;
        if (ch4.enabled && ch4.length > 0 && --ch4.length == 0) ch4.enabled = false;
    }
    if (frame_seq_step == 7) {
        // Clock envelope
        if (ch1.enabled && ch1.env_timer > 0) {
            if (--ch1.env_timer == 0) {
                ch1.env_timer = apu_regs[2] & 0x07;
                if (ch1.env_timer > 0 || (apu_regs[2] & 0x07) == 0) {
                    int add = (apu_regs[2] & 0x08) ? 1 : -1;
                    int newvol = ch1.env_vol + add;
                    if (newvol >= 0 && newvol <= 15) ch1.env_vol = newvol;
                }
            }
        }
        if (ch2.enabled && ch2.env_timer > 0) {
            if (--ch2.env_timer == 0) {
                ch2.env_timer = apu_regs[7] & 0x07;
                if (ch2.env_timer > 0 || (apu_regs[7] & 0x07) == 0) {
                    int add = (apu_regs[7] & 0x08) ? 1 : -1;
                    int newvol = ch2.env_vol + add;
                    if (newvol >= 0 && newvol <= 15) ch2.env_vol = newvol;
                }
            }
        }
        if (ch4.enabled && ch4.env_timer > 0) {
            if (--ch4.env_timer == 0) {
                ch4.env_timer = apu_regs[0x11] & 0x07;
                if (ch4.env_timer > 0 || (apu_regs[0x11] & 0x07) == 0) {
                    int add = (apu_regs[0x11] & 0x08) ? 1 : -1;
                    int newvol = ch4.env_vol + add;
                    if (newvol >= 0 && newvol <= 15) ch4.env_vol = newvol;
                }
            }
        }
    }
    if (frame_seq_step == 2 || frame_seq_step == 6) {
        // Clock sweep (ch1 only)
        if (ch1.sweep_enabled && ch1.enabled) {
            int shift = apu_regs[0] & 0x07;
            int period = (apu_regs[0] >> 4) & 0x07;
            int negate = (apu_regs[0] >> 3) & 0x01;
            if (period > 0) {
                if (--ch1.sweep_timer <= 0) {
                    ch1.sweep_timer = period;
                    if (shift > 0) {
                        int delta = ch1.sweep_freq >> shift;
                        if (negate) ch1.sweep_freq -= delta;
                        else ch1.sweep_freq += delta;
                        if (ch1.sweep_freq > 2047 || ch1.sweep_freq < 0) {
                            ch1.enabled = false;
                        } else {
                            ch1.period = (2048 - ch1.sweep_freq) * 2;
                        }
                    }
                }
            }
        }
    }
}

static inline int16_t square_sample(square_ch_t *ch, int reg_idx) {
    if (!ch->enabled || !ch->dac_enabled) return 0;
    if (ch->timer == 0 || ch->period == 0) return 0;
    if (--ch->timer <= 0) {
        ch->timer = ch->period;
        ch->seq_pos = (ch->seq_pos + 1) & 7;
    }
    int duty = (apu_regs[reg_idx] >> 6) & 3;
    int bit = duty_table[duty][ch->seq_pos];
    return bit ? (ch->env_vol * 2 - 15) * 512 : -ch->env_vol * 512;
}

static inline int16_t wave_sample(void) {
    if (!ch3.enabled || !ch3.dac_enabled) return 0;
    if (--ch3.timer <= 0) {
        ch3.timer = ch3.period;
        ch3.pos = (ch3.pos + 1) & 31;
    }
    int shift = ((apu_regs[0x0C] >> 5) & 3);
    if (shift == 0) return 0; // Mute
    shift = shift - 1;
    int byte = wave_ram[ch3.pos / 2];
    int nibble = (ch3.pos & 1) ? (byte & 0x0F) : (byte >> 4);
    return ((nibble - 8) << shift) * 256;
}

static inline int16_t noise_sample(void) {
    if (!ch4.enabled || !ch4.dac_enabled) return 0;
    if (ch4.period == 0) return 0;
    if (--ch4.timer <= 0) {
        ch4.timer = ch4.period;
        int width = (apu_regs[0x12] >> 3) & 1;
        int bit = ((ch4.lfsr >> 0) ^ (ch4.lfsr >> (width ? 6 : 1))) & 1;
        ch4.lfsr = (ch4.lfsr >> 1) | (bit << 14);
        if (width) ch4.lfsr = (ch4.lfsr & ~(1 << 6)) | (bit << 6);
    }
    int out = (~ch4.lfsr) & 1;
    return out ? (ch4.env_vol * 2 - 15) * 512 : -ch4.env_vol * 512;
}

static int16_t mix_sample(void) {
    if (!(apu_regs[0x16] & 0x80)) return 0; // Global sound off (NR52)

    int16_t ch1_s = square_sample(&ch1, 1);
    int16_t ch2_s = square_sample(&ch2, 6);
    int16_t ch3_s = wave_sample();
    int16_t ch4_s = noise_sample();

    // NR51 (0xFF25) controls panning — we sum both left and right
    int16_t left = 0, right = 0;
    if (apu_regs[0x15] & 0x01) left  += ch1_s;
    if (apu_regs[0x15] & 0x02) left  += ch2_s;
    if (apu_regs[0x15] & 0x04) left  += ch3_s;
    if (apu_regs[0x15] & 0x08) left  += ch4_s;
    if (apu_regs[0x15] & 0x10) right += ch1_s;
    if (apu_regs[0x15] & 0x20) right += ch2_s;
    if (apu_regs[0x15] & 0x40) right += ch3_s;
    if (apu_regs[0x15] & 0x80) right += ch4_s;

    // NR50 (0xFF24) controls master volume
    int vol_l = (apu_regs[0x14] >> 4) & 7;
    int vol_r = apu_regs[0x14] & 7;

    int16_t mixed = (int16_t)((left * vol_l + right * vol_r) / 14);
    return mixed;
}

#define APU_CPU_DIVIDER 8192  // 4194304 Hz CPU / 512 Hz frame seq

void gb_apu_reset(void) {
    memset(apu_regs, 0, sizeof(apu_regs));
    memset(wave_ram, 0, sizeof(wave_ram));
    memset(&ch1, 0, sizeof(ch1));
    memset(&ch2, 0, sizeof(ch2));
    memset(&ch3, 0, sizeof(ch3));
    memset(&ch4, 0, sizeof(ch4));
    frame_seq_timer = 0;
    frame_seq_step = 0;
    apu_regs[0x16] = 0x80; // NR52: sound on
}

// Generate one APU sample. Call at 44100 Hz.
int16_t gb_apu_step(void) {
    // Frame sequencer at 512 Hz
    frame_seq_timer += 44100;
    if (frame_seq_timer >= 4194304) {
        frame_seq_timer -= 4194304;
        apu_frame_seq();
    }

    return mix_sample();
}

#endif // GB_APU_H
