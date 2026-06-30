#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "os_core.h"
#include "loader.h"
#include "z80.h"
#include "spscr_p.h"
#include "spperif.h"
#include "misc.h"

typedef struct {
    const byte *buf;
    size_t size;
    size_t pos;
} buf_reader_t;

static int br_getc(buf_reader_t *r)
{
    if (r->pos >= r->size) return EOF;
    return r->buf[r->pos++];
}

static size_t br_read(buf_reader_t *r, void *dest, size_t n)
{
    size_t avail = r->size - r->pos;
    size_t to_read = n < avail ? n : avail;
    memcpy(dest, r->buf + r->pos, to_read);
    r->pos += to_read;
    return to_read;
}

static void decompress_ed_block_file(FILE *fp, byte *dest, unsigned dest_size, int compressed)
{
    if (!compressed) {
        fread(dest, 1, dest_size, fp);
        return;
    }

    unsigned pos = 0;
    while (pos < dest_size) {
        int c = fgetc(fp);
        if (c == EOF) break;
        if (c != 0xED) {
            dest[pos++] = (byte)c;
        } else {
            int next = fgetc(fp);
            if (next == EOF) break;
            if (next == 0xED) {
                int count = fgetc(fp);
                if (count == EOF) break;
                if (count == 0) break;
                int val = fgetc(fp);
                if (val == EOF) break;
                for (int i = 0; i < count && pos < dest_size; i++)
                    dest[pos++] = (byte)val;
            } else {
                dest[pos++] = 0xED;
                dest[pos++] = (byte)next;
            }
        }
    }
}

static void decompress_ed_block_buf(buf_reader_t *r, byte *dest, unsigned dest_size, int compressed)
{
    if (!compressed) {
        br_read(r, dest, dest_size);
        return;
    }

    unsigned pos = 0;
    while (pos < dest_size) {
        int c = br_getc(r);
        if (c == EOF) break;
        if (c != 0xED) {
            dest[pos++] = (byte)c;
        } else {
            int next = br_getc(r);
            if (next == EOF) break;
            if (next == 0xED) {
                int count = br_getc(r);
                if (count == EOF) break;
                if (count == 0) break;
                int val = br_getc(r);
                if (val == EOF) break;
                for (int i = 0; i < count && pos < dest_size; i++)
                    dest[pos++] = (byte)val;
            } else {
                dest[pos++] = 0xED;
                dest[pos++] = (byte)next;
            }
        }
    }
}

// Common header struct for Z80 snapshots
typedef struct {
    byte a, f;
    byte bc_l, bc_h;
    byte hl_l, hl_h;
    byte pcl, pch;
    byte spl, sph;
    byte i;
    byte r;
    byte data;
    byte de_l, de_h;
    byte bc2_l, bc2_h;
    byte de2_l, de2_h;
    byte hl2_l, hl2_h;
    byte a2, f2;
    byte iyl, iyh;
    byte ixl, ixh;
    byte iff1, iff2;
    byte im;
} z80_hdr_t;

static int parse_and_apply_header(const z80_hdr_t *hdr)
{
    RA = hdr->a;
    RF = hdr->f;
    RB = hdr->bc_h;
    RC = hdr->bc_l;
    RH = hdr->hl_h;
    RL = hdr->hl_l;
    RD = hdr->de_h;
    RE = hdr->de_l;
    SPL = hdr->spl;
    SPH = hdr->sph;
    RI = hdr->i;
    RR = (hdr->r & 0x7F) | ((hdr->data & 1) << 7);

    ABK = hdr->a2;
    FBK = hdr->f2;
    BBK = hdr->bc2_h;
    CBK = hdr->bc2_l;
    DBK = hdr->de2_h;
    EBK = hdr->de2_l;
    HBK = hdr->hl2_h;
    LBK = hdr->hl2_l;

    YH = hdr->iyh;
    YL = hdr->iyl;
    XH = hdr->ixh;
    XL = hdr->ixl;

    z80_proc.iff1 = hdr->iff1 ? 1 : 0;
    z80_proc.iff2 = hdr->iff2 ? 1 : 0;
    z80_proc.it_mode = hdr->im & 3;
    z80_proc.haltstate = 0;
    z80_proc.ula_outport = (z80_proc.ula_outport & ~7) | ((hdr->data >> 1) & 7);

    return 0;
}

int loader_load_z80_from_buffer(const byte *buf, size_t size)
{
    buf_reader_t r;
    r.buf = buf;
    r.size = size;
    r.pos = 0;

    z80_hdr_t hdr;
    if (br_read(&r, &hdr, sizeof(hdr)) != sizeof(hdr))
        return -1;

    int is_v2 = (hdr.pch == 0 && hdr.pcl == 0);

    if (is_v2) {
        int len_l = br_getc(&r);
        int len_h = br_getc(&r);
        if (len_l == EOF || len_h == EOF) return -1;
        int ext_len = len_l | (len_h << 8);
        if (ext_len < 23) return -1;

        byte ext[55];
        unsigned ext_read = (unsigned)br_read(&r, ext, sizeof(ext) > (unsigned)ext_len ? (unsigned)ext_len : sizeof(ext));

        for (unsigned i = ext_read; i < (unsigned)ext_len; i++) br_getc(&r);

        PCL = ext[0];
        PCH = ext[1];

        int hardware = (ext_len > 2) ? ext[2] : 0;
        if (hardware >= 4) return -1;

        int if1_paged = (ext_len > 6) ? (ext[6] & 1) : 0;
        if (if1_paged) return -1;

        while (1) {
            int pl = br_getc(&r);
            int ph = br_getc(&r);
            if (pl == EOF || ph == EOF) break;
            int page_len = pl | (ph << 8);
            int page_num = br_getc(&r);
            if (page_num == EOF) break;

            unsigned dest_offset;
            switch (page_num) {
                case 4: dest_offset = 0x8000; break;
                case 5: dest_offset = 0xC000; break;
                case 8: dest_offset = 0x4000; break;
                default:
                    for (int i = 0; i < page_len; i++) br_getc(&r);
                    continue;
            }

            decompress_ed_block_buf(&r, z80_proc.mem + dest_offset, 0x4000, 1);
        }
    } else {
        if (hdr.data == 0xFF) hdr.data = 1;
        int compressed = (hdr.data >> 5) & 1;
        PCL = hdr.pcl;
        PCH = hdr.pch;
        decompress_ed_block_buf(&r, z80_proc.mem + 0x4000, 0xC000, compressed);
    }

    parse_and_apply_header(&hdr);
    sp_init_screen_mark();
    return 0;
}

int loader_load_z80(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { os_log("LOADER", "fopen failed: %s", path); return -1; }

    os_log("LOADER", "loading %s", path);

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fp); return -1; }

    byte *buf = (byte *)malloc((size_t)fsize);
    if (!buf) { fclose(fp); return -1; }

    size_t nread = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);

    if (nread != (size_t)fsize) { free(buf); return -1; }

    int ret = loader_load_z80_from_buffer(buf, (size_t)fsize);
    free(buf);

    os_log("LOADER", "load %s", ret == 0 ? "OK" : "FAIL");
    return ret;
}

int loader_get_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return LOADER_UNKNOWN;
    if (mis_strcasecmp(dot + 1, "z80") == 0) return LOADER_Z80;
    if (mis_strcasecmp(dot + 1, "tap") == 0) return LOADER_TAP;
    if (mis_strcasecmp(dot + 1, "sna") == 0) return LOADER_SNA;
    return LOADER_UNKNOWN;
}
