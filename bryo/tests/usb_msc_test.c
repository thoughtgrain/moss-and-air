/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the drive's USB side: the descriptors usb.c sends with BRYO_MSC (audio + MIDI + Mass Storage), parsed
 * as a host does, and the Bulk-Only Transport in msc.c driven packet by packet against a RAM disk: INQUIRY, TEST
 * UNIT READY while the disk is being made, REQUEST SENSE, READ CAPACITY, WRITE(10) then READ(10) back, a command it
 * doesn't know, a read out of range, and eject. Build: cc -DT_UAC=0/1 tests/usb_msc_test.c */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define HALF_FRAMES 128
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC T_UAC
#define BRYO_MSC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */

/* the RAM disk behind it */
#define DISK_N 64u
static uint8_t disk[DISK_N][512];
static int disk_ready = 1, ejected_calls, attached_calls;
static uint32_t msc_blocks(void) { return DISK_N; }
static int msc_ready(void) { return disk_ready; }
static void msc_read(uint32_t lba, uint8_t *b) { memcpy(b, disk[lba], 512); }
static void msc_write(uint32_t lba, const uint8_t *b) { memcpy(disk[lba], b, 512); }
static void msc_eject(void) { ejected_calls++; }
static void msc_attached(void) { attached_calls++; }
#include "../firmware/src/usb.c"
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) {}

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* the host side of one command: a CBW, then data out (wdata) or in (into rdata), then the CSW; returns its status */
static uint32_t tag = 0x1000;
static int bot(const uint8_t *cb, uint32_t cblen, uint32_t len, int in, const uint8_t *wdata, uint8_t *rdata,
               uint32_t *got)
{
    uint8_t cbw[31] = {'U', 'S', 'B', 'C'}, p[64];
    uint32_t n = 0, k;
    tag++;
    cbw[4] = (uint8_t)tag;
    cbw[5] = (uint8_t)(tag >> 8);
    cbw[8] = (uint8_t)len;
    cbw[9] = (uint8_t)(len >> 8);
    cbw[10] = (uint8_t)(len >> 16);
    cbw[12] = in ? 0x80 : 0;
    cbw[14] = (uint8_t)cblen;
    memcpy(cbw + 15, cb, cblen);
    msc_rx(cbw, 31);
    if (!in)
        for (n = 0; n < len; n += 64)
            msc_rx(wdata + n, len - n < 64 ? len - n : 64);
    else
        while (msc.phase == MS_IN) {
            k = msc_tx(p);
            memcpy(rdata + n, p, k);
            n += k;
        }
    if (got)
        *got = n;
    k = msc_tx(p);
    if (k != 13 || memcmp(p, "USBS", 4) || p[4] != (uint8_t)tag || p[5] != (uint8_t)(tag >> 8))
        return -1;
    return p[12];
}

static int sense(uint8_t *sk, uint8_t *asc, uint8_t *ascq)
{
    static const uint8_t RS[6] = {0x03, 0, 0, 0, 18, 0};
    uint8_t b[512];
    int st = bot(RS, 6, 18, 1, 0, b, 0);
    *sk = b[2] & 15;
    *asc = b[12];
    *ascq = b[13];
    return st;
}

int main(void)
{
    const uint8_t *d;
    uint16_t l;
    uint8_t b[4096], w[1024], sk, asc, ascq;
    uint32_t got, i, nif = 0, msc_if = 99, ep_out = 0, ep_in = 0, total;
    int st;
    printf("-- the drive's USB side: UAC %d\n", T_UAC);
    get_desc(0x0100, &d, &l);
    check("device: class 0 (each interface says what it is), bcdDevice 3.x9", l == 18 && !d[4] && !d[5] && !d[6] &&
          d[12] == (0x10 * T_UAC + 0x09) && d[13] == 3);
    get_desc(0x0200, &d, &l);
    total = d[2] | d[3] << 8;
    check("configuration: wTotalLength matches what's sent", total == l);
    for (i = 0; i < l; i += d[i]) {
        if (!d[i])
            break;
        if (d[i + 1] == 4 && d[i + 3] == 0) {
            nif++;
            if (d[i + 5] == 0x08)
                msc_if = d[i + 2];
            if (d[i + 5] == 0x08)
                check("mass storage interface: SCSI transparent, bulk-only, 2 endpoints", d[i + 6] == 0x06 &&
                      d[i + 7] == 0x50 && d[i + 4] == 2);
        }
        if (d[i + 1] == 5 && msc_if != 99 && !ep_in) {
            if (d[i + 2] == 0x03 && d[i + 3] == 2 && d[i + 4] == 64)
                ep_out = 1;
            if (d[i + 2] == 0x83 && d[i + 3] == 2 && d[i + 4] == 64)
                ep_in = 1;
        }
    }
    check("interfaces: audio + MIDI first, the drive last", d[4] == nif && msc_if == nif - 1 && nif == 3u + T_UAC);
    check("..its endpoints: EP3 OUT and EP3 IN, bulk, 64 bytes", ep_out && ep_in);

    {
        static const uint8_t INQ[6] = {0x12, 0, 0, 0, 36, 0};
        st = bot(INQ, 6, 36, 1, 0, b, &got);
        check("INQUIRY: 36 bytes, a removable disk named BRYO, status good", st == 0 && got == 36 && b[0] == 0 &&
              b[1] == 0x80 && !memcmp(b + 8, "BRYO", 4));
    }
    {
        static const uint8_t TUR[6] = {0};
        disk_ready = 0;
        st = bot(TUR, 6, 0, 0, 0, 0, 0);
        check("TEST UNIT READY while the disk is being made: failed", st == 1);
        st = sense(&sk, &asc, &ascq);
        check("..REQUEST SENSE says NOT READY, becoming ready (02/04/01)", st == 0 && sk == 2 && asc == 4 && ascq == 1);
        disk_ready = 1;
        check("..then ready", bot(TUR, 6, 0, 0, 0, 0, 0) == 0);
    }
    {
        static const uint8_t CAP[10] = {0x25};
        st = bot(CAP, 10, 8, 1, 0, b, &got);
        check("READ CAPACITY: the last sector and 512-byte sectors", st == 0 && got == 8 && b[3] == DISK_N - 1 &&
              b[6] == 2 && b[7] == 0);
    }
    {
        static const uint8_t WR[10] = {0x2A, 0, 0, 0, 0, 5, 0, 0, 2, 0}, RD[10] = {0x28, 0, 0, 0, 0, 5, 0, 0, 2, 0};
        for (i = 0; i < 1024; i++)
            w[i] = (uint8_t)(i * 7 + 3);
        st = bot(WR, 10, 1024, 0, w, 0, 0);
        check("WRITE(10) of sectors 5 and 6: status good, the disk holds them", st == 0 && !memcmp(disk[5], w, 512) &&
              !memcmp(disk[6], w + 512, 512));
        memset(b, 0, sizeof b);
        st = bot(RD, 10, 1024, 1, 0, b, &got);
        check("READ(10) of them: the same 1,024 bytes", st == 0 && got == 1024 && !memcmp(b, w, 1024));
    }
    {
        static const uint8_t ODD[6] = {0xFF};
        st = bot(ODD, 6, 8, 1, 0, b, &got);
        check("a command the drive doesn't know: 8 zero bytes, then failed", st == 1 && got == 8 && !b[0]);
        st = sense(&sk, &asc, &ascq);
        check("..ILLEGAL REQUEST, invalid command (05/20/00)", st == 0 && sk == 5 && asc == 0x20);
    }
    {
        static const uint8_t RD[10] = {0x28, 0, 0, 0, 0, 63, 0, 0, 2, 0};
        st = bot(RD, 10, 1024, 1, 0, b, &got);
        check("a read past the end: the bytes asked for, then failed", st == 1 && got == 1024);
        st = sense(&sk, &asc, &ascq);
        check("..ILLEGAL REQUEST, out of range (05/21)", st == 0 && sk == 5 && asc == 0x21);
    }
    {
        static const uint8_t EJ[6] = {0x1B, 0, 0, 0, 0x02, 0}, TUR[6] = {0};
        st = bot(EJ, 6, 0, 0, 0, 0, 0);
        check("eject (START STOP UNIT, LoEj): good, the disk is told", st == 0 && ejected_calls == 1);
        st = bot(TUR, 6, 0, 0, 0, 0, 0);
        sense(&sk, &asc, &ascq);
        check("..then the medium is gone (02/3A) until plugged in again", st == 1 && sk == 2 && asc == 0x3A);
    }
    printf(fails ? "usb_msc: %d FAILED\n" : "usb_msc: all passed\n", fails);
    return fails != 0;
}
