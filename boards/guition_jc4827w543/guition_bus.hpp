// Guition JC4827W543 display: functional LovyanGFX bus + self-contained panel.
//
// The bus implements every IBus method using the proven raw spi_device driver.
// The panel overrides ONLY init (stable, self-contained) and inherits Panel_LCD
// for ALL drawing — including writeImage with anti-aliased font transparency.

#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

namespace guition {

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

static constexpr int GPIO_CS = 45;
static constexpr spi_host_device_t SPI_HOST = SPI3_HOST;
static constexpr uint32_t SPI_FREQ = 32000000;
static constexpr uint8_t CMD_IND = 0x02, DAT_IND = 0x32;
static constexpr uint32_t DAT_ADDR = 0x003C00;

class GuitionQSPIBus : public lgfx::IBus
{
public:
    lgfx::bus_type_t busType(void) const override { return lgfx::bus_type_t::bus_spi; }

    bool init(void) override
    {
        if (_dev) return true;
        spi_bus_config_t bc = {};
        bc.mosi_io_num=21; bc.miso_io_num=48; bc.sclk_io_num=47;
        bc.quadwp_io_num=40; bc.quadhd_io_num=39;
        bc.max_transfer_sz=BUF_BYTES; bc.flags=SPICOMMON_BUSFLAG_MASTER;
        esp_err_t r = spi_bus_initialize(SPI_HOST, &bc, SPI_DMA_CH_AUTO);
        if (r!=ESP_OK && r!=ESP_ERR_INVALID_STATE) return false;

        spi_device_interface_config_t dc = {};
        dc.command_bits=8; dc.address_bits=24; dc.dummy_bits=0; dc.mode=1;
        dc.clock_source=SPI_CLK_SRC_DEFAULT; dc.clock_speed_hz=SPI_FREQ;
        dc.spics_io_num=-1; dc.flags=SPI_DEVICE_HALFDUPLEX; dc.queue_size=1;
        if (spi_bus_add_device(SPI_HOST,&dc,&_dev)!=ESP_OK) return false;
        spi_device_acquire_bus(_dev,portMAX_DELAY);

        gpio_set_direction((gpio_num_t)GPIO_CS,GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)GPIO_CS,1);
        _buf=(uint8_t*)heap_caps_aligned_alloc(16,BUF_BYTES,MALLOC_CAP_DMA);
        if(_buf) memset(_buf,0,BUF_BYTES);
        return _buf!=nullptr;
    }

    void release(void) override {}
    void beginTransaction(void) override {}
    void endTransaction(void) override {}
    void wait(void) override {}
    bool busy(void) const override { return false; }
    void flush(void) override {}

    bool writeCommand(uint32_t data, uint_fast8_t) override
    { cs_lo(); xfer(true,false,CMD_IND,data<<8,nullptr,0); cs_hi(); return true; }

    // Combined command+value in ONE transaction (NV3041A requires this for
    // register writes — separate cmd then data is rejected).
    void writeCmdData(uint8_t cmd, const uint8_t *data, uint_fast8_t bitlen)
    { cs_lo(); xfer(true,false,CMD_IND,(uint32_t)cmd<<8,data,bitlen); cs_hi(); }

    void writeData(uint32_t data, uint_fast8_t bits) override
    {
        uint8_t b=(bits+7)/8; if(b>4)b=4;
        uint8_t t[4]={}; for(uint8_t i=0;i<b;i++) t[i]=data>>(8*(b-1-i));
        cs_lo(); xfer(true,true,DAT_IND,DAT_ADDR,t,bits); cs_hi();
    }

    void writeDataRepeat(uint32_t data, uint_fast8_t bits, uint32_t count) override
    {
        if(!count) return;
        uint8_t b=(bits+7)/8; uint8_t px[4]={};
        for(uint8_t i=0;i<b;i++) px[i]=data>>(8*(b-1-i));
        uint32_t c32=((uint32_t)px[1]<<24)|((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[0];
        uint32_t mp=BUF_BYTES/b; bool first=true;
        cs_lo();
        while(count){
            uint32_t n=(count<mp)?count:mp;
            for(uint32_t i=0;i<n/2;i++) ((uint32_t*)_buf)[i]=c32;
            if(n&1){_buf[(n-1)*b]=px[0];_buf[(n-1)*b+1]=px[1];}
            xfer(first,true,first?DAT_IND:0,first?DAT_ADDR:0,_buf,n*b*8);
            first=false; count-=n;
        }
        cs_hi();
    }

    void writeBytes(const uint8_t *data, uint32_t len, bool dc, bool) override
    {
        if(!len) return;
        bool first=true;
        cs_lo();
        while(len){
            uint32_t n=(len<BUF_BYTES)?len:BUF_BYTES;
            if(data!=_buf) memcpy(_buf,data,n);
            if(dc) {
                for(uint32_t i=0;i+1<n;i+=2){uint8_t t=_buf[i];_buf[i]=_buf[i+1];_buf[i+1]=t;}
            }
            xfer(first,true,(dc&&first)?DAT_IND:0,(dc&&first)?DAT_ADDR:0,_buf,n*8);
            first=false; len-=n; data+=n;
        }
        cs_hi();
    }

    void writePixels(lgfx::pixelcopy_t *p, uint32_t len) override
    {
        uint32_t b=p->dst_bits>>3; uint32_t mp=BUF_BYTES/b;
        while(len){uint32_t n=(len>mp)?mp:len; p->fp_copy(_buf,0,n,p); writeBytes(_buf,n*b,true,false); len-=n;}
    }

    void initDMA(void) override {}
    void addDMAQueue(const uint8_t*d,uint32_t l) override { writeBytes(d,l,true,false); }
    void execDMAQueue(void) override {}
    uint8_t *getDMABuffer(uint32_t) override { return _buf; }
    void beginRead(void) override {}
    void endRead(void) override {}
    uint32_t readData(uint_fast8_t) override { return 0; }
    bool readBytes(uint8_t*,uint32_t,bool=false) override { return false; }
    void readPixels(void*,lgfx::pixelcopy_t*,uint32_t) override {}

private:
    static constexpr uint32_t BUF_BYTES=8192;
    spi_device_handle_t _dev=nullptr;
    uint8_t *_buf=nullptr;
    inline void cs_lo(void){gpio_set_level((gpio_num_t)GPIO_CS,0);}
    inline void cs_hi(void){gpio_set_level((gpio_num_t)GPIO_CS,1);}
    void xfer(bool hc,bool qio,uint8_t cmd,uint32_t addr,const uint8_t*d,uint32_t bl)
    {
        spi_transaction_ext_t t={};
        if(bl==0) t.base.flags=SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR;
        else if(bl<=32){t.base.flags=SPI_TRANS_USE_TXDATA|(qio?SPI_TRANS_MODE_QIO:(SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR));for(uint32_t i=0;i<(bl+7)/8;i++)t.base.tx_data[i]=d[i];}
        else{t.base.flags=qio?SPI_TRANS_MODE_QIO:(SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR);t.base.tx_buffer=d;}
        if(hc){t.base.cmd=cmd;t.base.addr=addr;}
        else{t.base.flags|=SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY;t.command_bits=0;t.address_bits=0;t.dummy_bits=0;}
        t.base.length=bl;
        spi_device_polling_transmit(_dev,&t.base);
    }
};

// Panel: self-contained init (stable). Inherits ALL Panel_LCD drawing
// (setWindow, drawPixel, fillRect, writeImage with transparency, etc.).
class Panel_NV3041A_Guition : public lgfx::Panel_LCD
{
public:
    bool init(bool use_reset) override
    {
        if (!lgfx::Panel_Device::init(use_reset)) return false;
        auto *bus = static_cast<GuitionQSPIBus*>(_bus);
        bus->writeCmdData(0x01, nullptr, 0);  // SWRESET
        lgfx::delay(150);
        for(size_t i=0;i<sizeof(INIT_CMDS)/sizeof(INIT_CMDS[0]);i++){
            bus->writeCmdData(INIT_CMDS[i][0], &INIT_CMDS[i][1], 8);
            lgfx::delay(1);
        }
        lgfx::delay(120);
        bus->writeCommand(0x29,8);  // DISPON
        bus->writeCommand(0x21,8);  // INVON
        _write_depth=lgfx::color_depth_t::rgb565_nonswapped;
        _read_depth =lgfx::color_depth_t::rgb565_nonswapped;
        _write_bits =16;
        return true;
    }

    // Combined CASET/RASET (Panel_LCD's split set_window_8 doesn't work here).
    void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override
    {
        auto *bus = static_cast<GuitionQSPIBus*>(_bus);
        uint8_t c[4] = {(uint8_t)(xs>>8),(uint8_t)xs,(uint8_t)(xe>>8),(uint8_t)xe};
        uint8_t r[4] = {(uint8_t)(ys>>8),(uint8_t)ys,(uint8_t)(ye>>8),(uint8_t)ye};
        bus->writeCmdData(0x2A, c, 32);
        bus->writeCmdData(0x2B, r, 32);
        bus->writeCommand(0x2C, 8);
    }

    void update_madctl(void) override
    {
        auto *bus = static_cast<GuitionQSPIBus*>(_bus);
        uint8_t rgb=(_cfg.rgb_order?0x00:0x08),v;
        switch(_internal_rotation){case 1:v=0x80|0x20|rgb;break;case 2:v=rgb;break;case 3:v=0x40|0x20|rgb;break;default:v=0x40|0x80|rgb;break;}
        bus->writeCmdData(0x36, &v, 8);
    }

    void setInvert(bool) override {}
    void setSleep(bool) override {}
    lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override
    {
        _write_depth=lgfx::color_depth_t::rgb565_nonswapped;
        _read_depth =lgfx::color_depth_t::rgb565_nonswapped;
        return _write_depth;
    }
};

} // namespace guition
