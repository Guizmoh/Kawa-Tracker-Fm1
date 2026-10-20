/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the update loader (firmware/loader/ldr_core.c), Felucca's ported to KAWA: a fake host serves a
 * package over the SysEx protocol, the "device" flash starts as another package's flash.bin (as if that
 * firmware were installed), with the update records of step 1 and user data in KAWA's store.
 * Power cuts: a longjmp out of a flash erase, the flash as it was at that instant; "power on" runs the
 * loader again (the SPL does, while the flash record is there).
 *   ldr_test OLD.fwsc NEW.fwsc   (tests/run_loader_tests.sh) */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, erases, progs, bad_range, record_cleared, f0_asked;
static uint32_t flip_addr = 0xFFFFFFFFu, flip_seen, flip_lo, flip_hi;   /* serve addr's flip_lo..hi-th replies changed */
static uint32_t trunc_at = 0xFFFFFFFFu;         /* the package served as 0xFF from here (a truncated file) */
static uint32_t no_success;                     /* the host never answers 0xF0000000 */
static uint32_t cut_at = 0xFFFFFFFFu;           /* power fails halfway through the erase of this sector */
static uint32_t erased_at[64], n_erased;        /* the sectors erased in this session */
static int flash_known = 1;
static jmp_buf power;

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64] = {0}, m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        if (addr == 0xF0000000u && no_success) return 0;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
        for (i = 0; i < len; i++)
            if (addr + i >= trunc_at) m[14 + i] = 0xFF;
        if (addr == flip_addr && ++flip_seen >= flip_lo && flip_seen <= flip_hi)
            m[14 + len / 2u] ^= 0x10;                /* a transfer error the frame checksum does not see */
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../firmware/src/ota.c"

/* the loader's windows (loader.c FL_RANGE_OK): erase [0x4000, 0x97000) and [0xE0000, 0xFC000), program the app
 * area only; never the head, KAWA's store [0x97000, 0xE0000) or the settings (0xFC000) */
static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u || off >= 0xFC000u || (off & 0xFFFu) || (off >= 0x97000u && off < 0xE0000u)) bad_range++;
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    if (off == cut_at) {                         /* the power goes: half of the sector erased */
        memset(nor + off, 0xFF, 0x800);
        cut_at = 0xFFFFFFFFu;
        longjmp(power, 1);
    }
    erases++;
    if (n_erased < 64u) erased_at[n_erased++] = off;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    if (off < 0x4000u || off + n > 0x93000u || ((off & 0xFFu) + n) > 256u) bad_range++;
    if (off < 0x4000u) { printf("PROGRAM IN THE HEAD %#x\n", off); exit(1); }
    progs++;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static int ldr_flash_known(void) { return flash_known; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../firmware/loader/ldr_core.c"

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    if (n < 20 * 48 + 0x400) { printf("%s: too short for a package\n", path); exit(2); }
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}

static uint32_t flash_off(const uint8_t *lg)        /* flash.bin offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

static int check(const char *what, int ok) { printf("%-78s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

static uint8_t *old, *newf, rec[112], head[0x4000], user_a[0x1000], user_d[0x1000], sett[0x1000];
static uint32_t ofo, nfo;

static int record_at(uint32_t at)                   /* a valid update record there (the SPL would run the loader) */
{
    return ota_rd16(nor + at + 6) == 0x5441u && ota_rd16(nor + at) == ota_crc16(nor + at + 2, 78, 0);
}

/* OLD installed, the records of step 1 at 0xE4F00 (KAWA's / Felucca's) and 0xE8F00 (the stock firmware's,
 * a first install from it); user data in KAWA's store (a project slot, the last user-sample sector) and the
 * settings, each with bytes that look like a record */
static void device_old(void)
{
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, old + ofo, 0x93000);
    memcpy(nor + 0xE4F00, rec, sizeof rec);
    memcpy(nor + 0xE8F00, rec, sizeof rec);
    for (i = 0; i < 0x1000u; i++) {
        nor[0xA0000 + i] = (uint8_t)(i * 7u);
        nor[0xDF000 + i] = (uint8_t)(i * 13u + 1u);
        nor[0xFC000 + i] = (uint8_t)(i * 5u + 3u);
    }
    memcpy(nor + 0xA0F00, rec, sizeof rec);
    memcpy(nor + 0xDFF00, rec, sizeof rec);
    memcpy(nor + 0xFCF00, rec, sizeof rec);
    memcpy(user_a, nor + 0xA0000, sizeof user_a);
    memcpy(user_d, nor + 0xDF000, sizeof user_d);
    memcpy(sett, nor + 0xFC000, sizeof sett);
    memcpy(head, nor, sizeof head);
}

static void session_reset(void)
{
    rx_full = 0; requests = 0; erases = 0; progs = 0; n_erased = 0; record_cleared = 0; f0_asked = 0;
    flip_seen = 0;
}

static int untouched(void)                          /* the head, the store and the settings as they were */
{
    return !memcmp(nor, head, sizeof head) && !memcmp(nor + 0xA0000, user_a, sizeof user_a) &&
           !memcmp(nor + 0xDF000, user_d, sizeof user_d) && !memcmp(nor + 0xFC000, sett, sizeof sett);
}

static int erased(uint32_t s)
{
    uint32_t i;
    for (i = 0; i < n_erased; i++)
        if (erased_at[i] == s) return 1;
    return 0;
}

static int cut_session(void)                        /* 1: the power went during the session */
{
    if (setjmp(power))
        return 1;
    ldr_session();
    return 0;
}

int main(int argc, char **argv)
{
    size_t olen;
    uint32_t i, ndiff = 0, diff[4] = {0};
    int bad = 0, rc;
    if (argc < 3) { printf("usage: ldr_test OLD.fwsc NEW.fwsc\n"); return 2; }
    old = load_logical(argv[1], &olen);
    logical = load_logical(argv[2], &logical_len);
    ofo = flash_off(old);
    nfo = flash_off(logical);
    newf = logical + nfo;
    if (!ofo || !nfo || ofo + 0x93000u > olen || nfo + 0x93000u > logical_len) { printf("no flash.bin\n"); return 2; }
    for (i = 0x4000; i < 0x93000u && ndiff < 4u; i += 0x1000u)   /* app sectors the update has to write */
        if (memcmp(old + ofo + i, newf + i, 0x1000))
            diff[ndiff++] = i;
    if (ndiff < 4u) { printf("the two packages differ in %u sectors: too alike for the test\n", ndiff); return 2; }
    rec[2] = 0x0D; rec[3] = 0x5A; rec[4] = 0x01; rec[5] = 0x5A; rec[6] = 0x41; rec[7] = 0x54;
    ota_wr16(rec, ota_crc16(rec + 2, 78, 0));
    device_old();

    /* refused before anything is erased: a damaged package, a truncated one, one for another chip key */
    {
        size_t at = nfo + 0x50123u;
        session_reset();
        logical[at] ^= 0x01;                         /* one byte deep in the app area */
        rc = ldr_session();
        logical[at] ^= 0x01;
        bad += check("damaged package (flash.bin CRC16) refused, nothing erased or written",
                     rc == LDR_EBADPKG && erases == 0 && progs == 0 && !memcmp(nor, old + ofo, 0x93000) &&
                     record_at(0xE4F00) && untouched());
        bad += check("  ... checked three times (a byte damaged on the way is served again)",
                     requests >= 3u * (0x93000u / 512u));
        session_reset();
        trunc_at = nfo + 0x60000u;
        rc = ldr_session();
        trunc_at = 0xFFFFFFFFu;
        bad += check("truncated package (its tail served as 0xFF) refused, nothing erased",
                     rc == LDR_EBADPKG && erases == 0 && progs == 0 && !memcmp(nor, old + ofo, 0x93000));
        session_reset();
        logical[nfo + 0x4000 + 5] ^= 0x5A;           /* the app area head no longer decrypts */
        rc = ldr_session();
        logical[nfo + 0x4000 + 5] ^= 0x5A;
        bad += check("foreign chip key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
    }
    /* a sector that comes in different on the write pass every time: read again, never written; the record stays */
    session_reset();
    flip_addr = nfo + diff[1]; flip_lo = 2; flip_hi = 0xFFFFFFFFu;
    rc = ldr_session();
    flip_addr = 0xFFFFFFFFu;
    bad += check("a sector served differently on every write-pass read: refused, not written",
                 rc == LDR_ESECTOR && flip_seen == 4 && !erased(diff[1]) && progs > 0 &&
                 !memcmp(nor + diff[1], old + ofo + diff[1], 0x1000));
    bad += check("  ... the sectors before it written, the record kept (the host can resume)",
                 !memcmp(nor + diff[0], newf + diff[0], 0x1000) && record_at(0xE4F00) && record_cleared == 0);
    /* the power goes halfway through an erase */
    session_reset();
    cut_at = diff[2];
    bad += check("power cut halfway through an erase: the record is still in flash",
                 cut_session() && record_at(0xE4F00) && record_at(0xE8F00) && untouched());
    /* power on: the SPL runs the loader again; the host serves it all but never says "success" */
    session_reset();
    no_success = 1;
    flip_addr = nfo + diff[3]; flip_lo = flip_hi = 2;
    rc = ldr_session();
    no_success = 0;
    bad += check("the interrupted pass resumes: the app area is the new package's",
                 !memcmp(nor + 0x4000, newf + 0x4000, 0x93000 - 0x4000) && erased(diff[2]));
    bad += check("  ... a sector changed in transfer after the check is read again, then written",
                 flip_seen == 3 && erased(diff[3]));
    bad += check("  ... no \"success\": -13, the records kept (the loader stays)",
                 rc == LDR_ENOFIN && f0_asked >= 4 && record_cleared == 0 && record_at(0xE4F00) && record_at(0xE8F00));
    flip_addr = 0xFFFFFFFFu;
    /* the host comes back and finishes */
    session_reset();
    rc = ldr_session();
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("install completes, nothing left to erase", rc == 0 && erases == 2);
    bad += check("app area == the new package's flash.bin", !memcmp(nor + 0x4000, newf + 0x4000, 0x93000 - 0x4000));
    bad += check("finish asked (0xF0000000)", f0_asked >= 1);
    bad += check("update records cleared (RAM + flash 0xE4F00, the stock firmware's 0xE8F00)",
                 record_cleared && nor[0xE4F06] == 0xFF && nor[0xE8F06] == 0xFF && erased(0xE4000) && erased(0xE8000));
    bad += check("flash head [0, 0x4000), the store (0xA0000, 0xDF000) and the settings kept", untouched());
    bad += check("no write outside the loader's windows", bad_range == 0);
    session_reset();
    rc = ldr_session();
    bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0 && progs == 0);
    /* another flash chip: nothing written, the records go, the old firmware starts again */
    device_old();
    session_reset();
    flash_known = 0;
    rc = ldr_session();
    flash_known = 1;
    bad += check("unknown flash chip: nothing written in the app area, the old firmware kept",
                 rc == LDR_EFLASH && progs == 0 && !memcmp(nor, old + ofo, 0x93000));
    bad += check("  ... only the update records erased (0xE4000, 0xE8000), the store kept",
                 n_erased == 2 && erased(0xE4000) && erased(0xE8000) && untouched() && record_cleared);
    bad += check("no write outside the loader's windows", bad_range == 0);
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}
