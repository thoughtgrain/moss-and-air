/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: USB Mass Storage, Bulk-Only Transport with the SCSI commands a computer sends a removable drive. No
 * register access here: usb.c hands each 64-byte EP3 OUT packet to msc_rx and asks msc_tx for the next EP3 IN
 * packet, one each per poll (TIMER5, 2 kHz: about 128 KB/s each way). The disk behind it is four hooks, defined
 * before this file is included (vdisk.c in the app, a RAM disk in tests/usb_msc_test.c):
 *
 *   msc_blocks()          sectors of 512 bytes
 *   msc_ready()           0 while the disk is being made or busy (the computer is told "becoming ready", retries)
 *   msc_read(lba, b)      msc_write(lba, b)      msc_eject()
 *
 * A transfer: a 31-byte command block (CBW), the data (in or out), a 13-byte status (CSW). A command Bryo doesn't
 * know fails with ILLEGAL REQUEST; its data phase is still run to the length the computer asked for (zeros in,
 * dropped out), so the endpoints never need a stall. */

#define MSC_PKT 64u
#define MSC_SEC 512u
enum { MS_CBW, MS_IN, MS_OUT, MS_CSW };

static struct {
    uint8_t phase, status, op;
    uint8_t sk, asc, ascq;                   /* the sense to report next */
    uint8_t ejected;
    uint32_t tag, left;                      /* the data bytes still to move */
    uint32_t lba, nsec;                      /* READ / WRITE: the next sector, how many left */
    uint32_t bpos, blen;                     /* in buf: the next byte, how many are valid */
    uint8_t buf[MSC_SEC];
    uint32_t cmds, errs;
} msc;

static uint32_t msc_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void msc_wbe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void msc_reset(void)
{
    msc.phase = MS_CBW;
    msc.left = 0;
}

static void msc_sense(uint32_t sk, uint32_t asc, uint32_t ascq)
{
    msc.sk = (uint8_t)sk;
    msc.asc = (uint8_t)asc;
    msc.ascq = (uint8_t)ascq;
    msc.status = sk ? 1u : 0u;
}

/* a reply of n bytes in buf, sent as up to `left` bytes (padded with zeros when the computer asked for more) */
static void msc_reply(uint32_t n)
{
    uint32_t i;
    for (i = n; i < MSC_SEC; i++)
        msc.buf[i] = 0;
    msc.blen = MSC_SEC;
    msc.bpos = 0;
    msc.nsec = 0;
}

static void msc_command(const uint8_t *cbw)
{
    const uint8_t *cb = cbw + 15;
    uint32_t i, out = cbw[12] & 0x80u ? 0u : 1u, n = 0;
    uint8_t psk = msc.sk, pasc = msc.asc, pascq = msc.ascq;   /* (REQUEST SENSE reports the last command's) */
    msc.tag = (uint32_t)cbw[4] | (uint32_t)cbw[5] << 8 | (uint32_t)cbw[6] << 16 | (uint32_t)cbw[7] << 24;
    msc.left = (uint32_t)cbw[8] | (uint32_t)cbw[9] << 8 | (uint32_t)cbw[10] << 16 | (uint32_t)cbw[11] << 24;
    msc.op = cb[0];
    msc.nsec = 0;
    msc.cmds++;
    msc_sense(0, 0, 0);
    for (i = 0; i < MSC_SEC; i++)
        msc.buf[i] = 0;
    if (msc.ejected && cb[0] != 0x12 && cb[0] != 0x03) {
        msc_sense(2, 0x3A, 0);                    /* medium not present (ejected until plugged in again) */
    } else if (!msc_ready() && cb[0] != 0x12 && cb[0] != 0x03) {
        msc_sense(2, 0x04, 0x01);                 /* becoming ready */
    } else {
        switch (cb[0]) {
        case 0x00:                                /* TEST UNIT READY */
        case 0x1E:                                /* PREVENT / ALLOW MEDIUM REMOVAL */
        case 0x2F:                                /* VERIFY(10) */
        case 0x35:                                /* SYNCHRONIZE CACHE(10) */
            break;
        case 0x1B:                                /* START STOP UNIT: eject */
            if ((cb[4] & 3u) == 2u) {
                msc.ejected = 1;
                msc_eject();
            }
            break;
        case 0x03:                                /* REQUEST SENSE: the last command's */
            msc.buf[0] = 0x70;
            msc.buf[2] = psk;
            msc.buf[7] = 10;
            msc.buf[12] = pasc;
            msc.buf[13] = pascq;
            n = 18;
            break;
        case 0x12: {                              /* INQUIRY: a removable direct-access device */
            static const char ID[28] = "BRYO    FM-1 DRIVE      0.1 ";
            msc.buf[1] = 0x80;
            msc.buf[2] = 0x04;
            msc.buf[3] = 0x02;
            msc.buf[4] = 31;
            for (i = 0; i < 28u; i++)
                msc.buf[8 + i] = (uint8_t)ID[i];
            n = 36;
            break;
        }
        case 0x1A:                                /* MODE SENSE(6): no pages, writable */
            msc.buf[0] = 3;
            n = 4;
            break;
        case 0x5A:                                /* MODE SENSE(10) */
            msc.buf[1] = 6;
            n = 8;
            break;
        case 0x23:                                /* READ FORMAT CAPACITIES */
            msc.buf[3] = 8;
            msc_wbe32(msc.buf + 4, msc_blocks());
            msc.buf[8] = 2;                       /* formatted media */
            msc.buf[10] = MSC_SEC >> 8;
            n = 12;
            break;
        case 0x25:                                /* READ CAPACITY(10) */
            msc_wbe32(msc.buf, msc_blocks() - 1u);
            msc_wbe32(msc.buf + 4, MSC_SEC);
            n = 8;
            break;
        case 0x28:                                /* READ(10) */
        case 0x2A:                                /* WRITE(10) */
            msc.lba = msc_be32(cb + 2);
            msc.nsec = (uint32_t)cb[7] << 8 | cb[8];
            if (msc.lba + msc.nsec > msc_blocks() || msc.left != msc.nsec * MSC_SEC) {
                msc_sense(5, 0x21, 0);            /* out of range, or a length that doesn't match */
                msc.nsec = 0;
            }
            break;
        default:
            msc_sense(5, 0x20, 0);                /* invalid command */
            msc.errs++;
            break;
        }
    }
    msc.bpos = msc.blen = 0;
    if (!msc.left) {
        msc.phase = MS_CSW;
    } else if (out) {
        msc.phase = MS_OUT;
    } else {
        msc.phase = MS_IN;
        if (!msc.nsec)
            msc_reply(n);
    }
}

/* an EP3 OUT packet */
static void msc_rx(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (msc.phase == MS_CBW) {
        if (n == 31u && p[0] == 'U' && p[1] == 'S' && p[2] == 'B' && p[3] == 'C')
            msc_command(p);
        return;                                   /* (anything else between commands is dropped) */
    }
    if (msc.phase != MS_OUT)
        return;
    for (i = 0; i < n && msc.left; i++, msc.left--) {
        msc.buf[msc.bpos++] = p[i];
        if (msc.bpos == MSC_SEC) {                /* a whole sector: written, or dropped (a failed command) */
            if (msc.nsec) {
                msc_write(msc.lba++, msc.buf);
                msc.nsec--;
            }
            msc.bpos = 0;
        }
    }
    if (!msc.left)
        msc.phase = MS_CSW;
}

/* the next EP3 IN packet into p: its length, 0 for nothing to send */
static uint32_t msc_tx(uint8_t *p)
{
    uint32_t n = 0;
    if (msc.phase == MS_IN) {
        if (msc.bpos == msc.blen) {               /* the next sector, or zeros past the reply */
            if (msc.nsec) {
                msc_read(msc.lba++, msc.buf);
                msc.nsec--;
            } else {
                uint32_t i;
                for (i = 0; i < MSC_SEC; i++)
                    msc.buf[i] = 0;
            }
            msc.bpos = 0;
            msc.blen = MSC_SEC;
        }
        while (n < MSC_PKT && msc.left && msc.bpos < msc.blen) {
            p[n++] = msc.buf[msc.bpos++];
            msc.left--;
        }
        if (!msc.left)
            msc.phase = MS_CSW;
        return n;
    }
    if (msc.phase == MS_CSW) {
        p[0] = 'U';
        p[1] = 'S';
        p[2] = 'B';
        p[3] = 'S';
        p[4] = (uint8_t)msc.tag;
        p[5] = (uint8_t)(msc.tag >> 8);
        p[6] = (uint8_t)(msc.tag >> 16);
        p[7] = (uint8_t)(msc.tag >> 24);
        p[8] = p[9] = p[10] = p[11] = 0;          /* residue: every byte asked for was moved */
        p[12] = msc.status;
        msc.phase = MS_CBW;
        return 13;
    }
    return 0;
}
