/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the user's samples over USB-MIDI, from the emulator's page (Web MIDI) or tools/kawa_samples.py.
 * Felucca's way (its editor's SMP_* commands, hardware-checked): one request, one reply; the slot's header is
 * erased first and written last, after the CRC of what reached the flash is checked, so a cut upload leaves
 * an empty slot, never a broken one. The audio interrupt reads the slots where they are (memory-mapped flash).
 *
 * Frames: F0 7D 4B 57 cmd pack7(args) F7 ("KW", 7D: the non-commercial id; pack7: the updater's bit stream,
 * ota.c: 7 bits at a time, the low ones first). Every reply: the same cmd, pack7(rc, ...). Commands:
 *   1 INFO                           -> rc, "KAWA", version (4 chars), slots, capacity (3 B LE)
 *   2 BEGIN slot                     -> rc, slot         (stopped only: the sound holds while the flash is written)
 *   3 WRITE slot off(3 B LE) data    -> rc, slot, off    (off from 0, a multiple of 256, in order; data <= 256 B)
 *   4 END slot header(32 B)          -> rc, slot         (usr_hdr_t: magic, len, rate, name, crc)
 *   5 ERASE slot                     -> rc, slot
 *   6 LIST                           -> rc, then per slot: used, rate (2 B), len (3 B), name (12 B)
 * Codes: 0 ok, 1 bad arguments, 2 busy (playing), 3 flash, 4 CRC, 5 no session / out of order. */
enum { KW_INFO = 1, KW_BEGIN, KW_WRITE, KW_END, KW_ERASE, KW_LIST };
enum { KW_OK, KW_EARGS, KW_EBUSY, KW_EFLASH, KW_ECRC, KW_ESTATE };
#define USR_AT_SLOT(k) (USR_AT + (uint32_t)(k) * USR_SIZE)
typedef char usr_fits_the_store[USR_AT_SLOT(NUSR) <= STORE_HI ? 1 : -1];
typedef char usr_slots_on_sectors[(USR_AT & 0xFFFu) == 0u && (USR_SIZE & 0xFFFu) == 0u ? 1 : -1];

static uint32_t kw_pack7(const uint8_t *in, uint32_t n, uint8_t *out)   /* (as ota.c's) */
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8u;
        while (nb >= 7u) { out[o++] = (uint8_t)(acc & 0x7Fu); acc >>= 7; nb -= 7u; }
    }
    if (nb)
        out[o++] = (uint8_t)(acc & 0x7Fu);
    return o;
}
static uint32_t kw_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7u;
        if (nb >= 8u) {
            if (o < max) out[o] = (uint8_t)acc;
            o++;
            acc >>= 8;
            nb -= 8u;
        }
    }
    return o;
}

/* ---- the flash, or a stand-in for it on the computer (tests/host.c "sysex": tests/samples_test.py) */
#if defined(DINGHY_HOST) || defined(KAWA_WASM)
static uint8_t kw_flash[STORE_HI - USR_AT];
static int kw_erase(uint32_t off) { uint32_t i; for (i = 0; i < 0x1000u; i++) kw_flash[off - USR_AT + i] = 0xFF; return 0; }
static int kw_prog(uint32_t off, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) kw_flash[off - USR_AT + i] &= p[i];   /* NOR: programming only clears bits */
    return 0;
}
static int kw_read(uint32_t off, void *p, uint32_t n) { memcpy(p, kw_flash + off - USR_AT, n); return 0; }
static const uint8_t *kw_xip(uint32_t off) { return kw_flash + off - USR_AT; }
static void kw_hold_wait(void) {}
#if defined(DINGHY_HOST)
static void kw_send(const uint8_t *p, uint32_t n)   /* the reply, for the test script */
{
    uint32_t i;
    printf("%6u ms  sysex <", fm1_ms);
    for (i = 0; i < n; i++) printf(" %02X", p[i]);
    printf("\n");
}
#else
static void kw_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; }
#endif
#else
static void kw_send(const uint8_t *p, uint32_t n) { ota_wire_send(p, n); }
static int kw_erase(uint32_t off) { uint32_t took; fm1_wdt_feed(); return fl_erase4k(off, &took); }
static int kw_prog(uint32_t off, const uint8_t *p, uint32_t n) { return fl_write(off, p, n); }
static int kw_read(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static const uint8_t *kw_xip(uint32_t off) { return (const uint8_t *)FM1_XIP(off); }
static void kw_hold_wait(void)                  /* two blocks: the interrupt is silent, the DMA plays zeros */
{
    uint32_t b = audio_blocks, t0 = fm1_ms;
    while (audio_blocks - b < 3u && fm1_ms - t0 < 60u)
        fm1_wdt_feed();
}
#endif

static uint32_t kw_crc(const uint8_t *p, uint32_t n) { return ~crc_step(0xFFFFFFFFu, p, n); }

static int usr_check(uint32_t k, const usr_hdr_t *h)   /* a slot's header and its sound agree */
{
    if (h->magic != USR_MAGIC || h->len < 64u || h->len > USR_CAP || (h->rate != 22050u && h->rate != 11025u))
        return 0;
    return kw_crc(kw_xip(USR_AT_SLOT(k) + USR_DATA), h->len) == h->crc;
}

static void usr_boot(void)                      /* at start: the slots that hold a sound */
{
    uint32_t k;
#if !defined(DINGHY_HOST) && !defined(KAWA_WASM)
    if (!flash_ok)
        return;
    fl_plain_window_init();                     /* the data area reads as it was written (Felucca's persist_boot) */
#endif
    for (k = 0; k < NUSR; k++) {
        usr_hdr_t h;
        usr_data[k] = 0;
        if (kw_read(USR_AT_SLOT(k), &h, sizeof h) || !usr_check(k, &h))
            continue;
        h.name[11] = 0;
        usr_hdr[k] = h;
        usr_data[k] = kw_xip(USR_AT_SLOT(k) + USR_DATA);
    }
}

/* ---- the session: one slot at a time; the sound holds from BEGIN to END (10 s without a frame: given up) */
static uint8_t kw_slot = 0xFF;
static uint32_t kw_next, kw_ms;
static uint8_t kw_dec[300], kw_wire[8 + 120];
static uint8_t kw_last_rc;

static void kw_reply(uint32_t cmd, const uint8_t *b, uint32_t n)   /* n <= 100 */
{
    uint32_t w = 5;
    kw_wire[0] = 0xF0;
    kw_wire[1] = 0x7D;
    kw_wire[2] = 0x4B;
    kw_wire[3] = 0x57;
    kw_wire[4] = (uint8_t)cmd;
    w += kw_pack7(b, n, kw_wire + 5);
    kw_wire[w++] = 0xF7;
    kw_send(kw_wire, w);
}

static void kw_end_session(void)
{
    kw_slot = 0xFF;
    audio_hold = 0;
}

static int kw_take_slot(uint32_t k)             /* stop what reads it, hold the sound */
{
    if (k >= NUSR)
        return KW_EARGS;
    if (sq_play)
        return KW_EBUSY;
    usr_data[k] = 0;
    smp_kill = 1;
    kw_hold_wait();
    audio_hold = 1;
    kw_hold_wait();
    return KW_OK;
}

static void kw_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t r[100];
    uint32_t k = n ? a[0] : 0xFFu, i;
    r[0] = KW_OK;
    r[1] = (uint8_t)k;
    kw_ms = fm1_ms;
    switch (cmd) {
    case KW_INFO: {
        static const char V[] = APP_VER "    ";
        r[1] = 'K'; r[2] = 'A'; r[3] = 'W'; r[4] = 'A';
        for (i = 0; i < 4u; i++) r[5 + i] = (uint8_t)V[i];
        r[9] = NUSR;
        r[10] = (uint8_t)USR_CAP; r[11] = (uint8_t)(USR_CAP >> 8); r[12] = (uint8_t)(USR_CAP >> 16);
        kw_reply(cmd, r, 13);
        return;
    }
    case KW_BEGIN:
        if (n != 1u) { r[0] = KW_EARGS; break; }
        if (kw_slot != 0xFFu && kw_slot != k) kw_end_session();
        if ((r[0] = (uint8_t)kw_take_slot(k)) != KW_OK) break;
        if (kw_erase(USR_AT_SLOT(k))) { r[0] = KW_EFLASH; kw_end_session(); break; }   /* the header first: empty */
        kw_slot = (uint8_t)k;
        kw_next = 0;
        break;
    case KW_WRITE: {
        uint32_t off = n >= 4u ? (uint32_t)a[1] | (uint32_t)a[2] << 8 | (uint32_t)a[3] << 16 : 0xFFFFFFFFu, len = n - 4u;
        r[2] = n >= 4u ? a[1] : 0; r[3] = n >= 4u ? a[2] : 0; r[4] = n >= 4u ? a[3] : 0;
        if (n < 5u || len > 256u || (off & 0xFFu) || off + len > USR_CAP) { r[0] = KW_EARGS; kw_reply(cmd, r, 5); return; }
        if (k != kw_slot) { r[0] = KW_ESTATE; kw_reply(cmd, r, 5); return; }
        if (off + 256u == kw_next) {            /* the same frame again (its reply was lost): already there */
            r[0] = kw_last_rc;
            kw_reply(cmd, r, 5);
            return;
        }
        if (off != kw_next) { r[0] = KW_ESTATE; kw_reply(cmd, r, 5); return; }
        {
            uint32_t at = USR_AT_SLOT(k) + USR_DATA + off;
            if (((at & 0xFFFu) == 0u || off == 0u) && (at & ~0xFFFu) != USR_AT_SLOT(k) && kw_erase(at & ~0xFFFu))
                r[0] = KW_EFLASH;               /* a new sector: erased first (the header's was in BEGIN) */
            else if (kw_prog(at, a + 4, len))
                r[0] = KW_EFLASH;
        }
        if (r[0] == KW_OK) kw_next = off + 256u;
        kw_last_rc = r[0];
        kw_reply(cmd, r, 5);
        return;
    }
    case KW_END: {
        usr_hdr_t h, back;
        if (n != 1u + sizeof h) { r[0] = KW_EARGS; break; }
        if (k != kw_slot) { r[0] = KW_ESTATE; break; }
        memcpy(&h, a + 1, sizeof h);
        h.name[11] = 0;
        if (h.len > kw_next || !usr_check(k, &h)) { r[0] = KW_ECRC; kw_end_session(); break; }
        if (kw_prog(USR_AT_SLOT(k), (const uint8_t *)&h, sizeof h) || kw_read(USR_AT_SLOT(k), &back, sizeof back) ||
            memcmp(&h, &back, sizeof h)) {
            r[0] = KW_EFLASH;
            kw_end_session();
            break;
        }
        usr_hdr[k] = h;
        usr_data[k] = kw_xip(USR_AT_SLOT(k) + USR_DATA);
        kw_end_session();
        wv_src = 0xFF;                          /* (the SOUND page draws it again) */
        toast(TR("SAMPLE RECU !", "SAMPLE IN !", "SAMPLE RECIBIDO !"));
        break;
    }
    case KW_ERASE:
        if (n != 1u) { r[0] = KW_EARGS; break; }
        if ((r[0] = (uint8_t)kw_take_slot(k)) != KW_OK) break;
        for (i = 0; i < USR_SIZE; i += 0x1000u)
            if (kw_erase(USR_AT_SLOT(k) + i)) { r[0] = KW_EFLASH; break; }
        kw_end_session();
        break;
    case KW_LIST:
        for (i = 0; i < NUSR; i++) {
            uint8_t *q = r + 1 + i * 18u;
            const usr_hdr_t *h = &usr_hdr[i];
            uint32_t used = usr_data[i] != 0, j;
            q[0] = (uint8_t)used;
            q[1] = (uint8_t)(used ? h->rate : 0); q[2] = (uint8_t)(used ? h->rate >> 8 : 0);
            q[3] = (uint8_t)(used ? h->len : 0); q[4] = (uint8_t)(used ? h->len >> 8 : 0); q[5] = (uint8_t)(used ? h->len >> 16 : 0);
            for (j = 0; j < 12u; j++) q[6 + j] = (uint8_t)(used ? h->name[j] : 0);
        }
        kw_reply(cmd, r, 1 + NUSR * 18u);
        return;
    default:
        r[0] = KW_EARGS;
        break;
    }
    kw_reply(cmd, r, 2);
}

/* a frame from the computer (the bytes between F0 and F7): ours (1), or the updater's (0: ota_service takes it) */
static int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t d;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Bu || p[2] != 0x57u)
        return 0;
    d = kw_unpack7(p + 4, n - 4u, kw_dec, sizeof kw_dec);
    if (d <= sizeof kw_dec)
        kw_handle(p[3], kw_dec, d);
    return 1;
}

static void kw_poll(void)                       /* main loop: a session left hanging gives the sound back */
{
    if (kw_slot != 0xFFu && fm1_ms - kw_ms > 10000u)
        kw_end_session();
}
