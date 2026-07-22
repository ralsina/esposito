#include "z80.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Z80 PRNM(proc);

byte PRNM(inports)[PORTNUM];
byte PRNM(outports)[PORTNUM];

#ifdef SPECT_MEM
#define NUM64KSEGS 1
#endif

#ifndef NUM64KSEGS
#define NUM64KSEGS 1
#endif

static byte *a64kmalloc(int num64ksegs)
{
    (void)num64ksegs;
    byte *mem = (byte *)calloc(0x10000, 1);
    return mem;
}

void PRNM(init)(void)
{
    qbyte i;

    DANM(mem) = a64kmalloc(0);
    for (i = 0; i < 0x10000; i++) DANM(mem)[i] = (byte)0;

    for (i = 0; i < NUMDREGS; i++) {
        DANM(nr)[i].p = DANM(mem);
        DANM(nr)[i].d.d = (dbyte)0;
    }

    for (i = 0; i < BACKDREGS; i++) {
        DANM(br)[i].p = DANM(mem);
        DANM(br)[i].d.d = (dbyte)0;
    }

    for (i = 0; i < PORTNUM; i++) PRNM(inports)[i] = PRNM(outports)[i] = 0;

    PRNM(local_init)();
}

void PRNM(nmi)(void)
{
    DANM(iff2) = DANM(iff1);
    DANM(iff1) = 0;
    DANM(haltstate) = 0;
    PRNM(pushpc)();
    PC = 0x0066;
}

void PRNM(interrupt)(int data)
{
    if (DANM(iff1)) {
        DANM(haltstate) = 0;
        DANM(iff1) = DANM(iff2) = 0;

        switch (DANM(it_mode)) {
        case 0:
            PRNM(pushpc)();
            PC = 0x0038;
            break;
        case 1:
            PRNM(pushpc)();
            PC = 0x0038;
            break;
        case 2:
            PRNM(pushpc)();
            PCL = DANM(mem)[(dbyte)(((int)RI << 8) + (data & 0xFF))];
            PCH = DANM(mem)[(dbyte)(((int)RI << 8) + (data & 0xFF) + 1)];
            break;
        }
    }
}

void PRNM(reset)(void)
{
    DANM(haltstate) = 0;
    DANM(iff1) = DANM(iff2) = 0;
    DANM(it_mode) = 0;
    RI = 0;
    RR = 0;
    PC = 0;
}
