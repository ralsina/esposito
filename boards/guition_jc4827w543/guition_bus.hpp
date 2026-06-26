// Guition JC4827W543 display: a self-contained LovyanGFX panel.
//
// LovyanGFX's own bus/init orchestration is flaky on this NV3041A QSPI panel
// (intermittent garbage). The bare ESP-IDF spi_device driver (Arduino_GFX
// framing) is rock-solid. So this panel does the SPI setup + panel init itself
// (the exact proven sequence) and implements every draw method by calling those
// same raw functions. LovyanGFX is kept only for the high-level pieces
// (fonts/sprites/JPEG) that sit on top of these primitives.

#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

namespace guition {

// NV3041A register/value init sequence (from Arduino_GFX).
static const uint8_t INIT_CMDS[][2] = {
    {0xff,0xa5},{0x36,0xc0},{0x3A,0x01},{0x41,0x03},{0x44,0x15},{0x45,0x15},
    {0x7d,0x03},{0xc1,0xbb},{0xc2,0x05},{0xc3,0x10},{0xc6,0x3e},{0xc7,0x25},
    {0xc8,0x11},{0x7a,0x5f},{0x6f,0x44},{0x78,0x70},{0xc9,0x00},{0x67,0x21},
    {0x51,0x0a},{0x52,0x76},{0x53,0x0a},{0x54,0x76},{0x46,0x0a},{0x47,0x2a},
    {0x48,0x0a},{0x49,0x1a},{0x56,0x43},{0x57,0x42},{0x58,0x3c},{0x59,0x64},
    {0x5a,0x41},{0x5b,0x3c},{0x5c,0x02},{0x5d,0x3c},{0x5e,0x1f},{0x60,0x80},
    {0x61,0x3f},{0x62,0x21},{0x63,0x07},{0x64,0xe0},{0x65,0x02},{0xca,0x20},
    {0xcb,0x52},{0xcc,0x10},{0xcD,0x42},{0xD0,0x20},{0xD1,0x52},{0xD2,0x10},
    {0xD3,0x42},{0xD4,0x0a},{0xD5,0x32},{0x80,0x00},{0xA0,0x00},{0x81,0x07},
    {0xA1,0x06},{0x82,0x02},{0xA2,0x01},{0x86,0x11},{0xA6,0x10},{0x87,0x27},
    {0xA7,0x27},{0x83,0x37},{0xA3,0x37},{0x84,0x35},{0xA4,0x35},{0x85,0x3f},
    {0xA5,0x3f},{0x88,0x0b},{0xA8,0x0b},{0x89,0x14},{0xA9,0x14},{0x8a,0x1a},
    {0xAa,0x1a},{0x8b,0x0a},{0xAb,0x0a},{0x8c,0x14},{0xAc,0x08},{0x8d,0x17},
    {0xAd,0x07},{0x8e,0x16},{0xAe,0x06},{0x8f,0x1B},{0xAf,0x07},{0x90,0x04},
    {0xB0,0x04},{0x91,0x0A},{0xB1,0x0A},{0x92,0x16},{0xB2,0x15},{0xff,0x00},
    {0x11,0x00},
};

static constexpr int      GPIO_CS  = 45;
static constexpr int      GPIO_BL  = 1;
static constexpr spi_host_device_t SPI_HOST = SPI3_HOST;
static constexpr uint32_t SPI_FREQ = 32000000;
static constexpr uint8_t  CMD_IND  = 0x02;
static constexpr uint8_t  DAT_IND  = 0x32;
static constexpr uint32_t DAT_ADDR = 0x003C00;

// A no-op bus so LovyanGFX's Panel_Device::init() has a valid _bus; the panel
// does all SPI itself, this is never used for data.
class Bus_Stub : public lgfx::IBus
{
public:
    lgfx::bus_type_t busType(void) const override { return lgfx::bus_type_t::bus_spi; }
    bool init(void) override { return true; }
    void release(void) override {}
    void beginTransaction(void) override {}
    void endTransaction(void) override {}
    void wait(void) override {}
    bool busy(void) const override { return false; }
    void flush(void) override {}
    bool writeCommand(uint32_t, uint_fast8_t) override { return true; }
    void writeData(uint32_t, uint_fast8_t) override {}
    void writeDataRepeat(uint32_t, uint_fast8_t, uint32_t) override {}
    void writePixels(lgfx::pixelcopy_t *, uint32_t) override {}
    void writeBytes(const uint8_t *, uint32_t, bool, bool) override {}
    void initDMA(void) override {}
    void addDMAQueue(const uint8_t *, uint32_t) override {}
    void execDMAQueue(void) override {}
    uint8_t *getDMABuffer(uint32_t) override { return nullptr; }
    void beginRead(void) override {}
    void endRead(void) override {}
    uint32_t readData(uint_fast8_t) override { return 0; }
    bool readBytes(uint8_t *, uint32_t, bool = false) override { return false; }
    void readPixels(void *, lgfx::pixelcopy_t *, uint32_t) override {}
};

class Panel_NV3041A_Guition : public lgfx::Panel_LCD
{
public:
    bool init(bool use_reset) override
    {
        // Panel_Device::init sets up the backlight and (no-op) cs/rst for pin=-1.
        // We do NOT use its bus -- the panel talks SPI directly below.
        if (!lgfx::Panel_Device::init(use_reset)) return false;

        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num   = 21;
        buscfg.miso_io_num   = 48;
        buscfg.sclk_io_num   = 47;
        buscfg.quadwp_io_num = 40;
        buscfg.quadhd_io_num = 39;
        buscfg.max_transfer_sz = BUF_BYTES;
        buscfg.flags = SPICOMMON_BUSFLAG_MASTER;
        esp_err_t ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return false;

        spi_device_interface_config_t devcfg = {};
        devcfg.command_bits = 8;
        devcfg.address_bits = 24;
        devcfg.dummy_bits = 0;
        devcfg.mode = 1;
        devcfg.clock_source = SPI_CLK_SRC_DEFAULT;
        devcfg.clock_speed_hz = SPI_FREQ;
        devcfg.spics_io_num = -1;
        devcfg.flags = SPI_DEVICE_HALFDUPLEX;
        devcfg.queue_size = 1;
        if (spi_bus_add_device(SPI_HOST, &devcfg, &_dev) != ESP_OK) return false;
        spi_device_acquire_bus(_dev, portMAX_DELAY);

        gpio_set_direction((gpio_num_t)GPIO_CS, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)GPIO_CS, 1);
        gpio_set_direction((gpio_num_t)GPIO_BL, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)GPIO_BL, 1);

        _buf = (uint8_t *)heap_caps_aligned_alloc(16, BUF_BYTES, MALLOC_CAP_DMA);
        if (!_buf) return false;
        memset(_buf, 0, BUF_BYTES);

        // Proven raw init sequence
        raw_command(0x01);                 // SWRESET
        lgfx::delay(150);
        for (size_t i = 0; i < sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]); i++) {
            raw_c8d8(INIT_CMDS[i][0], INIT_CMDS[i][1]);
            lgfx::delay(1);
        }
        lgfx::delay(120);
        raw_command(0x29);                 // DISPON
        raw_command(0x21);                 // INVON
        _write_depth = lgfx::color_depth_t::rgb565_nonswapped;
        _read_depth  = lgfx::color_depth_t::rgb565_nonswapped;
        _write_bits  = 16;
        return true;
    }

    // ---- drawing primitives (delegate to raw functions) ----
    void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override
    {
        raw_c8d16d16(0x2A, xs, xe);   // CASET
        raw_c8d16d16(0x2B, ys, ye);   // RASET
        raw_command(0x2C);            // RAMWR
    }

    void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override
    {
        static int _seen = 0;
        if (_seen < 3) { ESP_LOGI("guition", "drawPixelPreclipped rawcolor=0x%lx", (unsigned long)rawcolor); _seen++; }
        bool tr = _in_transaction;
        if (!tr) begin_transaction();
        setWindow(x, y, x, y);
        raw_pixel(rawcolor);
        if (!tr) end_transaction();
    }

    void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, uint32_t rawcolor) override
    {
        bool tr = _in_transaction;
        if (!tr) begin_transaction();
        setWindow(x, y, x + w - 1, y + h - 1);
        raw_repeat(rawcolor, (uint32_t)w * h);
        if (!tr) end_transaction();
    }

    void writeBlock(uint32_t rawcolor, uint32_t len) override
    {
        raw_repeat(rawcolor, len);
    }

    void writePixels(lgfx::pixelcopy_t *param, uint32_t len, bool /*use_dma*/) override
    {
        uint32_t bytes = param->dst_bits >> 3;
        uint32_t maxp = BUF_BYTES / bytes;
        while (len) {
            uint32_t n = (len > maxp) ? maxp : len;
            param->fp_copy(_buf, 0, n, param);
            raw_databuf(_buf, n * bytes);
            len -= n;
        }
    }

    void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, lgfx::pixelcopy_t *param, bool use_dma) override
    {
        bool tr = _in_transaction;
        if (!tr) begin_transaction();
        setWindow(x, y, x + w - 1, y + h - 1);
        writePixels(param, (uint32_t)w * h, use_dma);
        if (!tr) end_transaction();
    }

    // ---- config: the raw init already configured everything; ignore LovyanGFX ----
    void setInvert(bool) override {}
    void setSleep(bool) override {}
    lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override
    {
        _write_depth = lgfx::color_depth_t::rgb565_nonswapped;
        _read_depth  = lgfx::color_depth_t::rgb565_nonswapped;
        return _write_depth;
    }
    void update_madctl(void) override
    {
        uint8_t rgb = (_cfg.rgb_order ? 0x00 : 0x08);
        uint8_t r;
        switch (_internal_rotation) {
            case 1: r = 0x80 | 0x20 | rgb; break;
            case 2: r = rgb; break;
            case 3: r = 0x40 | 0x20 | rgb; break;
            default: r = 0x40 | 0x80 | rgb; break;
        }
        raw_c8d8(0x36, r);
    }

private:
    static constexpr uint32_t BUF_BYTES = 8192;
    spi_device_handle_t _dev = nullptr;
    uint8_t *_buf = nullptr;

    inline void cs_lo(void) { gpio_set_level((gpio_num_t)GPIO_CS, 0); }
    inline void cs_hi(void) { gpio_set_level((gpio_num_t)GPIO_CS, 1); }

    void xfer(uint8_t cmd, uint32_t addr, bool has_cmd,
              const uint8_t *data, int bitlen, bool qio)
    {
        spi_transaction_ext_t t = {};
        if (bitlen == 0) {
            t.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
        } else if (bitlen <= 32) {
            t.base.flags = SPI_TRANS_USE_TXDATA | (qio ? SPI_TRANS_MODE_QIO : (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR));
            for (int i = 0; i < (bitlen + 7) / 8; i++) t.base.tx_data[i] = data[i];
        } else {
            t.base.flags = qio ? SPI_TRANS_MODE_QIO : (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
            t.base.tx_buffer = data;
        }
        if (has_cmd) {
            t.base.cmd = cmd;
            t.base.addr = addr;
        } else {
            t.base.flags |= SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            t.command_bits = 0; t.address_bits = 0; t.dummy_bits = 0;
        }
        t.base.length = bitlen;
        spi_device_polling_transmit(_dev, &t.base);
    }

    void raw_command(uint8_t c)
    { cs_lo(); xfer(CMD_IND, (uint32_t)c << 8, true, nullptr, 0, false); cs_hi(); }

    void raw_c8d8(uint8_t c, uint8_t d)
    { uint8_t b[1] = {d}; cs_lo(); xfer(CMD_IND, (uint32_t)c << 8, true, b, 8, false); cs_hi(); }

    void raw_c8d16d16(uint8_t c, uint16_t d1, uint16_t d2)
    {
        uint8_t b[4] = { (uint8_t)(d1 >> 8), (uint8_t)d1, (uint8_t)(d2 >> 8), (uint8_t)d2 };
        cs_lo(); xfer(CMD_IND, (uint32_t)c << 8, true, b, 32, false); cs_hi();
    }

    void raw_pixel(uint16_t d)
    {
        uint8_t b[2] = { (uint8_t)(d >> 8), (uint8_t)d };
        cs_lo(); xfer(DAT_IND, DAT_ADDR, true, b, 16, true); cs_hi();
    }

    void raw_repeat(uint32_t color, uint32_t count)
    {
        if (!count) return;
        uint8_t px[2] = { (uint8_t)(color >> 8), (uint8_t)color };
        // Pack 2 pixels into a uint32 so memory (LE) yields [hi,lo,hi,lo] on wire
        uint32_t c32 = ((uint32_t)px[1] << 24) | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[0];
        uint32_t maxp = BUF_BYTES / 2;
        bool first = true;
        cs_lo();
        while (count) {
            uint32_t n = (count < maxp) ? count : maxp;
            for (uint32_t i = 0; i < n / 2; i++) ((uint32_t *)_buf)[i] = c32;
            if (n & 1) { _buf[(n - 1) * 2] = px[0]; _buf[(n - 1) * 2 + 1] = px[1]; }
            if (first) { xfer(DAT_IND, DAT_ADDR, true, _buf, n * 16, true); first = false; }
            else        { xfer(0, 0, false, _buf, n * 16, true); }
            count -= n;
        }
        cs_hi();
    }

    // raw byte buffer (LovyanGFX rgb565 buffers): send as-is, CS held across chunks
    void raw_databuf(const uint8_t *data, uint32_t len)
    {
        if (!len) return;
        bool first = true;
        cs_lo();
        while (len) {
            uint32_t n = (len < BUF_BYTES) ? len : BUF_BYTES;
            if (data != _buf) memcpy(_buf, data, n);
            if (first) { xfer(DAT_IND, DAT_ADDR, true, _buf, n * 8, true); first = false; }
            else        { xfer(0, 0, false, _buf, n * 8, true); }
            len -= n;
            data += n;
        }
        cs_hi();
    }
};

} // namespace guition
