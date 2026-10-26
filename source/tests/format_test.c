/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA's project format on this computer (tests/run_format_test.sh): projects saved by KAWA 0.2 (format 4,
 * tests/fixtures/, written by the 0.2 firmware's own prj_save) load into format 5 as they were, a damaged one
 * is refused, and a format-5 project with every new field comes back from its slot byte for byte. */
#define main kawa_host_main
#include "../firmware/src/kawa.c"
#undef main

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL  " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static int slot_from(uint32_t k, const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) { printf("FAIL  cannot open %s\n", path); fails++; return -1; }
    memset(host_flash[k], 0xFF, SLOT_SIZE);
    n = fread(host_flash[k], 1, SLOT_SIZE, f);
    fclose(f);
    return (int)n;
}

static void v5_defaults_check(const char *what)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        CHECK(!prj.tbl[t].on && prj.tbl[t].speed == 6u && prj.tbl[t].len == 4u, "%s: track %u's table not off", what, t + 1u);
        CHECK(prj.sed[t].start == 0u && prj.sed[t].end == 65535u && prj.sed[t].gain == 256u && !prj.sed[t].manual &&
              !prj.sed[t].rev && !prj.sed[t].norm, "%s: track %u's sample edit not whole", what, t + 1u);
        for (i = 0; i < 16u; i++)
            CHECK(prj.sed[t].sl[i] == i * 4096u, "%s: track %u's slice %u not even", what, t + 1u, i + 1u);
    }
    CHECK(!prj.grv.on, "%s: the groove is on", what);
}

int main(void)
{
    static project_t want;
    uint32_t p, r, t, k;
    int n;
    /* 1. the demo, saved by 0.2 */
    n = slot_from(1, "tests/fixtures/demo-v4.kawa");
    CHECK(n == 26248, "demo-v4: %d bytes, not 26248", n);
    CHECK(slot_valid(1, 0) == 4, "demo-v4: not seen as format 4");
    prj_demo();
    want = prj;
    want.bpm = 160;                             /* (the demo saved by 0.2 ran at 160; the demo now opens at 120) */
    prj_new();
    CHECK(prj_load(1) == 0, "demo-v4: refused");
    CHECK(prj.version == PRJ_VERSION, "demo-v4: still format %u", prj.version);
    CHECK(!memcmp(prj.name, want.name, 16), "demo-v4: the name");
    CHECK(prj.bpm == want.bpm && prj.song_len == want.song_len &&
          !memcmp(prj.song, want.song, want.song_len * sizeof prj.song[0]), "demo-v4: the song");
    CHECK(!memcmp(prj.snd, want.snd, sizeof prj.snd) && !memcmp(prj.fmp, want.fmp, sizeof prj.fmp), "demo-v4: the sounds");
    CHECK(!memcmp(prj.pat, want.pat, sizeof prj.pat), "demo-v4: the patterns are not the demo's");
    v5_defaults_check("demo-v4");
    printf("1. the demo saved by KAWA 0.2 loads as it was: %s\n", fails ? "NO" : "ok");
    /* 2. every trick and value of 0.2, on every track */
    n = slot_from(2, "tests/fixtures/tricks-v4.kawa");
    CHECK(prj_load(2) == 0, "tricks-v4: refused");
    for (r = 0, k = 0; r < NROW; r++)
        for (t = 0; t < NTRK; t++, k++) {
            const cell_t *c = &prj.pat[r % NPAT].c[r][t];
            uint32_t fx = k % 16u, note = k % 3u ? 40u + k % 60u : (k % 2u ? NOTE_OFF : 0u);
            int32_t fxv = clampi((int32_t)(k % 31u) - 15, FXD[fx].lo, FXD[fx].hi);
            CHECK(c->note == note && c->vol == 1u + k % 15u && c->fx == fx && (int8_t)c->fxv == fxv && !c->fx2 && !c->fxv2,
                  "tricks-v4: pattern %u row %u track %u: %u %u %u %d %u, want %u %u %u %d", r % NPAT + 1u, r + 1u, t + 1u,
                  c->note, c->vol, c->fx, (int8_t)c->fxv, c->fx2, note, 1u + k % 15u, fx, fxv);
        }
    for (p = 0; p < NPAT; p++)
        CHECK(prj.pat[p].len == 1u + p % 32u && prj.pat[p].live[p % NROW] == 1u + p, "tricks-v4: pattern %u's length, LIVE",
              p + 1u);
    v5_defaults_check("tricks-v4");
    printf("2. every 0.2 trick and value, every track, the LIVE lanes and lengths: %s\n", fails ? "NO" : "ok");
    /* 3. a damaged 0.2 project is refused (the CRC) */
    host_flash[2][700] ^= 0x10u;
    CHECK(slot_valid(2, 0) == 0 && prj_load(2) != 0, "tricks-v4 damaged: loaded");
    printf("3. a damaged 0.2 project is refused: %s\n", fails ? "NO" : "ok");
    /* 4. format 5 round trip: the second column, the tables, the sample edits, the groove */
    prj_demo();
    prj.pat[3].c[5][2].fx2 = FX_HOP;
    prj.pat[3].c[5][2].fxv2 = 7;
    prj.pat[4].c[31][5].fx2 = FX_END;
    prj.pat[0].c[0][0].fx2 = FX_VIB;
    prj.pat[0].c[0][0].fxv2 = 9;
    for (t = 0; t < NTRK; t++) {
        prj.tbl[t].on = 1;
        prj.tbl[t].speed = (uint8_t)(1u + t * 3u);
        prj.tbl[t].len = (uint8_t)(3u + t);
        prj.tbl[t].loop = (uint8_t)(t % 3u);
        for (k = 0; k < NTSTEP; k++) {
            prj.tbl[t].s[k].tr = (int8_t)((int32_t)(k * 3u % 25u) - 12);
            prj.tbl[t].s[k].vol = (uint8_t)(k % 16u);
            prj.tbl[t].s[k].fx = (uint8_t)(k % TB_N);
            prj.tbl[t].s[k].fxv = (int8_t)clampi((int32_t)k - 7, TBD[k % TB_N].lo, TBD[k % TB_N].hi);
        }
    }
    prj.sed[5].start = 4000;
    prj.sed[5].end = 60000;
    prj.sed[5].manual = 1;
    for (k = 0; k < 16u; k++) prj.sed[5].sl[k] = (uint16_t)(4000u + k * 3500u);
    prj.sed[5].rev = 1;
    prj.sed[5].norm = 1;
    prj.sed[5].gain = 700;
    prj.grv.on = 1;
    prj.grv.len = 3;
    prj.grv.tick[0] = 8; prj.grv.tick[1] = 4; prj.grv.tick[2] = 6;
    prj_sanitize();
    sed_normalise(5);
    CHECK(prj_save(3) == 0, "v5: the save failed");
    want = prj;                                 /* (as saved: its slot, its CRC) */
    CHECK(slot_valid(3, 0) == 5, "v5: not seen as format 5");
    prj_new();
    CHECK(prj_load(3) == 0, "v5: refused");
    for (k = 0; k < sizeof prj && ((uint8_t *)&prj)[k] == ((uint8_t *)&want)[k]; k++)
        ;
    CHECK(k == sizeof prj, "v5: did not come back as it was (byte %u of %u)", k, (unsigned)sizeof prj);
    printf("4. format 5 (two columns, tables, sample edits, groove) comes back byte for byte: %s\n", fails ? "NO" : "ok");
    printf("sizes: cell %u B, pattern %u B, project %u B of a %u B slot\n", (unsigned)sizeof(cell_t),
           (unsigned)sizeof(pat_t), (unsigned)sizeof(project_t), SLOT_SIZE);
    if (fails) { printf("format_test: %d failures\n", fails); return 1; }
    printf("format_test: passed\n");
    return 0;
}
