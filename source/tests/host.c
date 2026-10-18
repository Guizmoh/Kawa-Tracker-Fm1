/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 on a computer: KAWA (firmware/src/kawa.c) runs unchanged on it. platform.c includes this
 * file instead of the hardware when DINGHY_HOST is set. A script presses keys and buttons and turns
 * knobs over time; out come the screens (PPM, when the script says "shot") and the sound (WAV, as the
 * FM-1 sends it over USB). After Dinghy's tests/host.c (zvenson).
 *
 *   tests/run_host.sh [script]           (-> build/host/kawa.wav and the screens as PNG)
 *
 * Script lines: "T keys MASK" (bit k = key k, 0 = F3), "T press ID" / "T release ID" / "T btn ID" (a tap),
 * "T enc ID STEPS", "T lang L" (0 FR, 1 EN, 2 ES), "T shot NAME", "T sysex F0..F7" (a frame in hex; the
 * replies are printed), "T end". T in milliseconds. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "KAWA"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define FM1_TICKS_PER_US 24u
#define RING_PUBLISH() ((void)0)
#define STORE_LO 0x00097000u
#define STORE_HI 0x000E0000u

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_pressed, host_released, host_note_edges;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 24000u; }
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_pressed;
    *released = host_released;
    host_pressed = host_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t e = host_note_edges; host_note_edges = 0; return e; }
static int32_t fm1_enc_take(uint32_t e) { int32_t d = host_enc[e]; host_enc[e] = 0; return d; }
static uint8_t host_led_state[41];
static void host_leds(const uint8_t *w) { memcpy(host_led_state, w, 41); }
static unsigned str_len(const char *s) { return (unsigned)strlen(s); }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (byte-swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

static uint8_t host_settings[260];
static int settings_load(void *p, uint32_t n)   /* (as platform.c: the length saved must be n) */
{
    if (!host_settings[0] || n > 256u || host_settings[1] != n) return -1;
    memcpy(p, host_settings + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n) { host_settings[0] = 1; host_settings[1] = (uint8_t)n; memcpy(host_settings + 4, p, n); return 0; }
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2) { (void)st; (void)d1; (void)d2; return 0; }
static uint32_t host_midi_out[3];               /* MIDI out: counts of clock ticks, starts, stops (tests) */
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d1; (void)d2;
    if (st == 0xF8) host_midi_out[0]++;
    else if (st == 0xFA) { host_midi_out[1]++; printf("%6u ms  midi start\n", fm1_ms); }
    else if (st == 0xFC) { host_midi_out[2]++; printf("%6u ms  midi stop, %u clock ticks\n", fm1_ms, host_midi_out[0]); }
}

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static void set_lang(uint32_t l);
static int app_sysex(const uint8_t *p, uint32_t n);

typedef struct { uint32_t t; char what[12]; int32_t a, b; char name[32]; char *hex; } host_ev_t;
static host_ev_t ev[8192];
static uint32_t ev_n, ev_i, end_ms = 4000;
static const char *host_out = "build/host/kawa";
static int16_t *wav;
static uint32_t wav_n;

static void read_script(const char *path)
{
    char line[2048];
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && ev_n < 8192u) {
        host_ev_t e;
        memset(&e, 0, sizeof e);
        if (line[0] == '#' || sscanf(line, "%u %11s", &e.t, e.what) != 2) continue;
        if (!strcmp(e.what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(e.what, "shot")) sscanf(line, "%*u %*s %31s", e.name);
        else if (!strcmp(e.what, "sysex")) {
            char *q = strstr(line, "sysex") + 5;
            while (*q == ' ') q++;
            e.hex = strdup(q);
        }
        else if (!strcmp(e.what, "keys")) sscanf(line, "%*u %*s %i", &e.a);
        else sscanf(line, "%*u %*s %d %d", &e.a, &e.b);
        ev[ev_n++] = e;
    }
    fclose(f);
}

static void write_ppm(const char *tag)
{
    char name[512];
    FILE *f;
    uint32_t i;
    snprintf(name, sizeof name, "%s-%s.ppm", host_out, tag);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
    printf("%6u ms  screen -> %s\n", fm1_ms, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

static void app_poll(void);
static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t owed;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    app_poll();
    while (ev_i < ev_n && ev[ev_i].t <= fm1_ms) {
        host_ev_t *e = &ev[ev_i++];
        if (!strcmp(e->what, "keys")) { host_note_edges |= (uint32_t)e->a & ~fm1_in.notes; fm1_in.notes = (uint32_t)e->a; }
        else if (!strcmp(e->what, "press")) { host_pressed |= 1u << e->a; fm1_in.buttons |= 1u << e->a; }
        else if (!strcmp(e->what, "release")) { host_released |= 1u << e->a; fm1_in.buttons &= ~(1u << e->a); }
        else if (!strcmp(e->what, "btn")) { host_pressed |= 1u << e->a; host_released |= 1u << e->a; }
        else if (!strcmp(e->what, "enc")) host_enc[e->a] += e->b;
        else if (!strcmp(e->what, "lang")) set_lang((uint32_t)e->a);
        else if (!strcmp(e->what, "shot")) write_ppm(e->name);
        else if (!strcmp(e->what, "sysex")) {  /* a SysEx frame from the computer, F0 .. F7 in hex */
            uint8_t f[700];
            uint32_t n = 0;
            const char *q = e->hex;
            while (*q && n < sizeof f) {
                unsigned v;
                if (sscanf(q, "%2x", &v) != 1) break;
                f[n++] = (uint8_t)v;
                q += 2;
            }
            if (n >= 2u && f[0] == 0xF0 && f[n - 1u] == 0xF7 && !app_sysex(f + 1, n - 2u))
                printf("%6u ms  sysex: not KAWA's\n", fm1_ms);
        }
    }
    owed += FS;
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    app_main();
    return 0;
}
