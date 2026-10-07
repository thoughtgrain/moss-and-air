/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of usb.c's register side with the drive on: the real usb_start / usb_poll run against an emulated USB
 * controller, so the code between the registers and msc.c (EP0 requests, the EP3 bulk glue, bus reset) runs as it
 * does on the FM-1. tests/usb_msc_test.c covers the protocol above it.
 *
 * How the emulation works: hal/fm1_usb.h writes fixed addresses (0x10010, 0x11800.., 0x51000). I map those pages
 * at their real addresses; the USB page is read-only, so every register write faults. The fault handler makes the
 * page writable and single-steps the one store (the trap flag), then the trap handler looks at what was written
 * and does what the controller would: a CON1 write is a SIE register access (its result and the done bit go back
 * into CON1), an EP_CNT write starts a packet from that endpoint's DMA address. Reads need nothing: the page holds
 * what the controller would show. No thread, no timing: the same run every time.
 *
 * Linux x86-64 only (the trap flag, ucontext), built -no-pie so the static buffers' addresses fit the 32-bit DMA
 * registers. Elsewhere it says so and passes. Build: cc -no-pie -DT_UAC=0/1 tests/usb_sie_test.c */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__linux__) && defined(__x86_64__)
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>

/* the pi32v2 barriers usb.c issues (csync, ssync): nothing on the host */
__asm__(".macro csync\n.endm\n.macro ssync\n.endm\n");

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define HALF_FRAMES 128
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC T_UAC
#define BRYO_MSC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"

/* the RAM disk behind the drive */
#define DISK_N 64u
static uint8_t disk[DISK_N][512];
static int attached_calls, ejected_calls;
static uint32_t msc_blocks(void) { return DISK_N; }
static int msc_ready(void) { return 1; }
static void msc_read(uint32_t lba, uint8_t *b) { memcpy(b, disk[lba], 512); }
static void msc_write(uint32_t lba, const uint8_t *b) { memcpy(disk[lba], b, 512); }
static void msc_eject(void) { ejected_calls++; }
static void msc_attached(void) { attached_calls++; }
#include "../firmware/src/usb.c"
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) {}

/* ---------------------------------------------------------------- the controller ---- */
#define PG_USB 0x11000ul
#define REG(a) (*(volatile uint32_t *)(a))
#define A_CON0 0x11800ul
#define A_CON1 0x11804ul
#define A_EPCNT(n) (0x11808ul + 4ul * (n))
#define A_EP0ADR 0x11818ul
#define A_TADR(n) (0x1181Cul + 8ul * ((n) - 1ul))
#define A_RADR(n) (0x11820ul + 8ul * ((n) - 1ul))

static struct {
    uint8_t faddr, power, intrusb, intrtx, intrrx, index, intrusbe, intrtx1e, intrrx1e;
    uint8_t csr0, count0;
    uint8_t txcsr1[5], rxcsr1[5], rxcsr2[5], rxmaxp[5], txmaxp[5];
    uint16_t rxcount[5], frame;
    uint8_t pend[5][64];          /* the packet each IN endpoint's EP_CNT write started */
    uint32_t pend_n[5];
    uint32_t accesses;
} sie;

/* what the host saw */
static struct {
    uint8_t e0[1024];
    uint32_t e0_n;
    int e0_done, e0_stall;
    uint8_t in3[8192];            /* EP3 IN bytes, and each packet's length */
    uint32_t in3_n, pk[256], npk;
} host;

static uint32_t sie_read(uint32_t r)
{
    uint32_t i = sie.index, v;
    switch (r) {
    case S_FADDR: return sie.faddr;
    case S_POWER: return sie.power;
    case S_INTRUSB: v = sie.intrusb; sie.intrusb = 0; return v;   /* read clears */
    case S_INTRTX1: v = sie.intrtx; sie.intrtx = 0; return v;
    case S_INTRRX1: v = sie.intrrx; sie.intrrx = 0; return v;
    case S_FRAME1: return sie.frame & 0xFFu;
    case S_FRAME2: return sie.frame >> 8;
    case S_INDEX: return sie.index;
    case S_CSR0: return i ? sie.txcsr1[i] : sie.csr0;               /* (TXCSR1 shares the address) */
    case S_TXMAXP: return sie.txmaxp[i];
    case S_RXMAXP: return sie.rxmaxp[i];
    case S_RXCSR1: return sie.rxcsr1[i];
    case S_RXCSR2: return sie.rxcsr2[i];
    case S_COUNT0: return i ? sie.rxcount[i] & 0xFFu : sie.count0;
    case S_RXCOUNT2: return sie.rxcount[i] >> 8;
    default: return 0;
    }
}

/* the host takes the packet an IN endpoint was armed with */
static void host_takes(uint32_t ep)
{
    if (ep == 0) {
        memcpy(host.e0 + host.e0_n, sie.pend[0], sie.pend_n[0]);
        host.e0_n += sie.pend_n[0];
    } else if (ep == 3 && host.npk < 256) {
        memcpy(host.in3 + host.in3_n, sie.pend[3], sie.pend_n[3]);
        host.in3_n += sie.pend_n[3];
        host.pk[host.npk++] = sie.pend_n[3];
    }
    sie.pend_n[ep] = 0;
}

static void sie_write(uint32_t r, uint32_t v)
{
    uint32_t i = sie.index;
    switch (r) {
    case S_FADDR: sie.faddr = (uint8_t)v; break;
    case S_POWER: sie.power = (uint8_t)v; break;
    case S_INDEX: sie.index = (uint8_t)(v & 7u); break;
    case S_INTRUSBE: sie.intrusbe = (uint8_t)v; break;
    case S_INTRTX1E: sie.intrtx1e = (uint8_t)v; break;
    case S_INTRRX1E: sie.intrrx1e = (uint8_t)v; break;
    case S_TXMAXP: sie.txmaxp[i] = (uint8_t)v; break;
    case S_RXMAXP: sie.rxmaxp[i] = (uint8_t)v; break;
    case S_CSR0:
        if (i == 0) {
            if (!(v & 0x04u))
                sie.csr0 &= (uint8_t)~0x04u;             /* SentStall: written 0 clears it */
            if (v & 0x80u)
                sie.csr0 &= (uint8_t)~0x10u;             /* ServicedSetupEnd */
            if (v & 0x40u)
                sie.csr0 &= (uint8_t)~0x01u;             /* ServicedRxPktRdy */
            if (v & 0x20u) {                             /* SendStall: the host sees a STALL */
                host.e0_stall = 1;
                sie.csr0 |= 0x04u;
                sie.intrtx |= 1u;
            } else if (v & 0x02u) {                      /* TxPktRdy: the host reads it, then wants the next */
                host_takes(0);
                if (v & 0x08u)
                    host.e0_done = 1;                    /* (DataEnd: then the status stage) */
                sie.intrtx |= 1u;
            } else if (v & 0x08u) {                      /* DataEnd, no data: the status stage completes */
                host.e0_done = 1;
                sie.intrtx |= 1u;
            }
        } else {
            if (v & 0x01u)                               /* TxPktRdy: the host reads it at once */
                host_takes(i);
            sie.txcsr1[i] = (uint8_t)(v & ~0x49u);       /* (FlushFIFO / ClrDataTog self-clear) */
        }
        break;
    case S_RXCSR1:
        if (!(v & 0x01u) || (v & 0x10u))                 /* RxPktRdy cleared, or FlushFIFO (what usb.c writes, */
            sie.rxcsr1[i] &= (uint8_t)~0x01u;            /* as the SDK does): the buffer is the host's again */
        sie.rxcsr1[i] = (uint8_t)((sie.rxcsr1[i] & 0x01u) | (v & ~0x91u));
        break;
    case S_RXCSR2: sie.rxcsr2[i] = (uint8_t)v; break;
    default: break;
    }
}

/* one register write, after it happened */
static void reg_written(uintptr_t a)
{
    uint32_t v = REG(a);
    if (a == A_CON1) {
        if (!(REG(A_CON0) & 4u) || (v & 0x8000u))
            return;
        sie.accesses++;
        if (v & 0x4000u)                                 /* a read: result + done */
            REG(A_CON1) = (v & 0xFF00u) | 0x8000u | (sie_read((v >> 8) & 0x3Fu) & 0xFFu);
        else {
            sie_write((v >> 8) & 0x3Fu, v & 0xFFu);
            REG(A_CON1) = v | 0x8000u;
        }
    } else if (a >= A_EPCNT(0) && a <= A_EPCNT(3)) {   /* a packet starts: from EP0's or the TX DMA address */
        uint32_t ep = (uint32_t)(a - A_EPCNT(0)) / 4u, n = v > 64u ? 64u : v;
        const uint8_t *src = (const uint8_t *)(uintptr_t)(ep ? REG(A_TADR(ep)) : REG(A_EP0ADR));
        memcpy(sie.pend[ep], src, n);
        sie.pend_n[ep] = n;
    }
}

static volatile uintptr_t fault_at;
static void on_segv(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    uintptr_t a = (uintptr_t)si->si_addr;
    (void)sig;
    if ((a & ~0xFFFul) != PG_USB) {
        signal(SIGSEGV, SIG_DFL);                        /* a real crash: let it be one */
        return;
    }
    fault_at = a;
    mprotect((void *)PG_USB, 4096, PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100;             /* single-step the store */
}
static void on_trap(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    (void)sig;
    (void)si;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    reg_written(fault_at);
    mprotect((void *)PG_USB, 4096, PROT_READ);
}

static int emu_init(void)
{
    static const uintptr_t PG[3] = {0x10000ul, PG_USB, 0x51000ul};
    struct sigaction sa;
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (mmap((void *)PG[i], 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1,
                 0) != (void *)PG[i])
            return -1;
    if ((uintptr_t)ep3rx >> 32 || (uintptr_t)disk >> 32)
        return -1;                                       /* a PIE build: the DMA registers can't hold these */
    memset(&sa, 0, sizeof sa);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sa.sa_sigaction = on_segv;
    sigaction(SIGSEGV, &sa, 0);
    sa.sa_sigaction = on_trap;
    sigaction(SIGTRAP, &sa, 0);
    return mprotect((void *)PG_USB, 4096, PROT_READ);
}

/* ---------------------------------------------------------------- the host ---- */
static void poll_n(uint32_t n)
{
    while (n--) {
        sie.frame = (uint16_t)((sie.frame + 1u) & 0x7FFu);
        usb_poll();
    }
}

static void bus_reset(void)
{
    sie.intrusb |= 4u;
    poll_n(1);
}

/* a control transfer: the SETUP, then the device's polls until the status stage; the IN bytes, or -1 for a STALL */
static int control(uint8_t rt, uint8_t req, uint16_t wv, uint16_t wi, uint16_t wl, uint8_t *in)
{
    uint8_t s[8] = {rt, req, (uint8_t)wv, (uint8_t)(wv >> 8), (uint8_t)wi, (uint8_t)(wi >> 8), (uint8_t)wl,
                    (uint8_t)(wl >> 8)};
    uint32_t i;
    host.e0_n = 0;
    host.e0_done = host.e0_stall = 0;
    memcpy((uint8_t *)(uintptr_t)REG(A_EP0ADR), s, 8);
    sie.count0 = 8;
    sie.csr0 |= 1u;                                      /* RxPktRdy: a SETUP */
    sie.intrtx |= 1u;
    for (i = 0; i < 100u && !host.e0_done && !host.e0_stall; i++)
        poll_n(1);
    poll_n(2);                                           /* (the address lands after the status stage) */
    if (in)
        memcpy(in, host.e0, host.e0_n);
    return host.e0_stall ? -1 : host.e0_done ? (int)host.e0_n : -2;
}

/* one EP3 OUT packet: waits until the device has let go of the last one */
static int out3(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < 50u && (sie.rxcsr1[3] & 1u); i++)
        poll_n(1);
    if (sie.rxcsr1[3] & 1u)
        return -1;
    memcpy((uint8_t *)(uintptr_t)REG(A_RADR(3)), p, n);
    sie.rxcount[3] = (uint16_t)n;
    sie.rxcsr1[3] |= 1u;
    sie.intrrx |= 8u;
    return 0;
}

/* a whole bulk-only command over the wire; the CSW status, -1 for a broken CSW */
static uint32_t tag = 0x2000;
static int ubot(const uint8_t *cb, uint32_t cblen, uint32_t len, int in, const uint8_t *wd, uint8_t *rd, uint32_t *got)
{
    uint8_t cbw[31] = {'U', 'S', 'B', 'C'};
    uint32_t n, start, i, first;
    const uint8_t *csw;
    tag++;
    memcpy(cbw + 4, &tag, 4);
    memcpy(cbw + 8, &len, 4);
    cbw[12] = in ? 0x80 : 0;
    cbw[14] = (uint8_t)cblen;
    memcpy(cbw + 15, cb, cblen);
    host.in3_n = host.npk = 0;
    if (out3(cbw, 31))
        return -1;
    if (!in)
        for (n = 0; n < len; n += 64)
            if (out3(wd + n, len - n < 64 ? len - n : 64))
                return -1;
    for (i = 0; i < 4000u && host.in3_n < (in ? len : 0) + 13u; i++)
        poll_n(1);
    start = in ? len : 0;
    if (got)
        *got = host.in3_n >= 13u ? host.in3_n - 13u : 0;
    if (rd && in)
        memcpy(rd, host.in3, len);
    if (host.in3_n != start + 13u)
        return -1;
    first = host.npk ? host.pk[host.npk - 1] : 0;        /* the CSW: a packet of its own */
    csw = host.in3 + start;
    if (first != 13u || memcmp(csw, "USBS", 4) || memcmp(csw + 4, &tag, 4))
        return -1;
    return csw[12];
}

static int fails;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint8_t b[4096], w[2048];
    uint32_t got, i;
    int n, st;
    printf("-- usb.c against an emulated controller, the drive on: UAC %d\n", T_UAC);
    if (emu_init()) {
        printf("usb_sie: can't map the register pages here (needs Linux x86-64, -no-pie): skipped\n");
        return 0;
    }
    usb_start();
    check("usb_start: the SIE answers, USB is up, interrupts enabled", usb.up && !usb.timeouts &&
          sie.intrusbe == 0x07 && sie.intrtx1e == 0x01 && sie.accesses > 0);
    bus_reset();
    check("bus reset: address 0, the disk is told to make itself afresh", sie.faddr == 0 && attached_calls == 1);

    n = control(0x80, 6, 0x0100, 0, 64, b);
    check("GET_DESCRIPTOR device: the 18 bytes of the drive layout", n == 18 && !memcmp(b, DEV_DESC_MSC, 18));
    n = control(0x00, 5, 7, 0, 0, 0);
    check("SET_ADDRESS 7: taken after the status stage", n == 0 && sie.faddr == 7);
    n = control(0x80, 6, 0x0200, 0, 9, b);
    check("GET_DESCRIPTOR configuration, 9 bytes first", n == 9 && (uint32_t)(b[2] | b[3] << 8) == sizeof CFG_DESC_MSC);
    n = control(0x80, 6, 0x0200, 0, 1024, b);
    check("..then all of it, over several 64-byte packets", n == (int)sizeof CFG_DESC_MSC &&
          !memcmp(b, CFG_DESC_MSC, sizeof CFG_DESC_MSC) && sizeof CFG_DESC_MSC > 64);
    n = control(0x80, 6, 0x0300, 0, 255, b);
    check("GET_DESCRIPTOR string 0: the language", n == 4 && b[1] == 3);
    n = control(0x80, 6, 0x0700, 0, 64, b);
    check("an unknown descriptor: STALL, and the next request is answered", n == -1 && sie.csr0 == 0 &&
          control(0x80, 0, 0, 0, 2, b) == 2);
    n = control(0x00, 9, 1, 0, 0, 0);
    check("SET_CONFIGURATION 1: EP3 armed both ways, its DMA on the drive's buffers", n == 0 && usb.config == 1 &&
          sie.intrrx1e == 0x0A && REG(A_TADR(3)) == (uint32_t)(uintptr_t)ep3tx &&
          REG(A_RADR(3)) == (uint32_t)(uintptr_t)ep3rx);
    check("..EP3 enabled in CON0", !(REG(A_CON0) & (1u << (19 + 3))));
    n = control(0x80, 8, 0, 0, 1, b);
    check("GET_CONFIGURATION: 1", n == 1 && b[0] == 1);

    n = control(0xA1, 0xFE, 0, (uint16_t)(3 + T_UAC - 1), 1, b);
    check("GET MAX LUN: one byte, 0 (one drive)", n == 1 && b[0] == 0);

    {
        static const uint8_t INQ[6] = {0x12, 0, 0, 0, 36, 0};
        st = ubot(INQ, 6, 36, 1, 0, b, &got);
        check("INQUIRY over EP3: 36 bytes, then the CSW as a 13-byte packet of its own", st == 0 && got == 36 &&
              !memcmp(b + 8, "BRYO", 4));
    }
    {
        static const uint8_t WR[10] = {0x2A, 0, 0, 0, 0, 9, 0, 0, 3, 0}, RD[10] = {0x28, 0, 0, 0, 0, 9, 0, 0, 3, 0};
        for (i = 0; i < 1536u; i++)
            w[i] = (uint8_t)(i * 13 + 5);
        st = ubot(WR, 10, 1536, 0, w, 0, 0);
        check("WRITE(10) of 3 sectors in 24 packets: good, on the disk", st == 0 && !memcmp(disk[9], w, 512) &&
              !memcmp(disk[11], w + 1024, 512));
        memset(b, 0, sizeof b);
        st = ubot(RD, 10, 1536, 1, 0, b, &got);
        check("READ(10) of them: 24 full packets, the same bytes", st == 0 && got == 1536 && host.npk == 25 &&
              host.pk[0] == 64 && !memcmp(b, w, 1536));
    }
    {
        static const uint8_t MS6[6] = {0x1A, 0, 0x3F, 0, 192, 0}, MS10[10] = {0x5A, 0, 0x3F, 0, 0, 0, 0, 0, 192, 0},
                             RFC[10] = {0x23, 0, 0, 0, 0, 0, 0, 0, 252, 0};
        st = ubot(MS6, 6, 192, 1, 0, b, &got);
        check("MODE SENSE(6): a 4-byte header, no pages, not write protected (zeros after)", st == 0 && got == 192 &&
              b[0] == 3 && !(b[2] & 0x80) && !b[4]);
        st = ubot(MS10, 10, 192, 1, 0, b, &got);
        check("MODE SENSE(10): an 8-byte header, no pages", st == 0 && b[1] == 6 && !(b[3] & 0x80));
        st = ubot(RFC, 10, 252, 1, 0, b, &got);
        check("READ FORMAT CAPACITIES: one descriptor, the disk's sectors, formatted", st == 0 && b[3] == 8 &&
              (uint32_t)(b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7]) == DISK_N && b[8] == 2 && b[10] == 2);
    }
    {
        static const uint8_t RD[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 8, 0}, TUR[6] = {0};
        uint8_t cbw[31] = {'U', 'S', 'B', 'C', 1, 0, 0, 0, 0, 0x10, 0, 0, 0x80, 0, 10};
        memcpy(cbw + 15, RD, 10);
        host.in3_n = host.npk = 0;
        out3(cbw, 31);
        poll_n(4);
        n = control(0x21, 0xFF, 0, (uint16_t)(3 + T_UAC - 1), 0, 0);
        check("BULK-ONLY RESET in the middle of a read: acknowledged", n == 0 && host.npk >= 1 && host.npk < 64);
        poll_n(4);
        host.in3_n = host.npk = 0;
        st = ubot(TUR, 6, 0, 0, 0, 0, 0);
        check("..the read is dropped, the next command starts clean", st == 0 && host.in3_n == 13);
    }
    n = control(0x02, 1, 0, 0x83, 0, 0);
    check("CLEAR_FEATURE(HALT) on EP3 IN: acknowledged", n == 0);
    n = control(0x02, 1, 0, 0x05, 0, 0);
    check("..on an endpoint the drive doesn't have: STALL", n == -1);
    {
        static const uint8_t EJ[6] = {0x1B, 0, 0, 0, 2, 0}, TUR[6] = {0};
        st = ubot(EJ, 6, 0, 0, 0, 0, 0);
        check("eject: the disk is told, the medium is gone", st == 0 && ejected_calls == 1 &&
              ubot(TUR, 6, 0, 0, 0, 0, 0) == 1);
        bus_reset();
        check("plugged in again (bus reset): told to remount, not configured until the host says",
              attached_calls == 2 && !usb.config && sie.faddr == 0);
        control(0x00, 9, 1, 0, 0, 0);
        check("..configured again: the medium is back", ubot(TUR, 6, 0, 0, 0, 0, 0) == 0);
    }
    usb_msc_on = 0;
    n = control(0xA1, 0xFE, 0, 0, 1, b);
    check("the drive not presented: GET MAX LUN stalls", n == -1);
    usb_msc_on = 1;
    check("no SIE access ever timed out", !usb.timeouts && usb.up);
    printf(fails ? "usb_sie: %d FAILED\n" : "usb_sie: all passed\n", fails);
    return fails != 0;
}
#else
int main(void)
{
    printf("usb_sie: needs Linux x86-64: skipped\n");
    return 0;
}
#endif
