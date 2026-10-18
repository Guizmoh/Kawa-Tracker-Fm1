/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 in a browser: KAWA compiled to WebAssembly (clang --target=wasm32, no libc). platform.c
 * includes this file instead of the hardware when KAWA_WASM is set. The page's audio worklet runs it:
 * kawa_audio() renders 256 frames (the audio interrupt) and moves the clock, kawa_step() is one pass of
 * the main loop; the keys, buttons and knobs come in through kawa_keys / kawa_button / kawa_turn, the
 * screen (240 x 240 RGB565) and the LEDs go out through kawa_screen / kawa_leds. */
#include <stdint.h>
#define WASM_EXPORT(name) __attribute__((export_name(name)))

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
#include "libc.c"

static volatile uint32_t fm1_ms;
static uint64_t wasm_frames;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t w_pressed, w_released, w_note_edges;
static int32_t w_enc[7];
static uint8_t w_leds[41];
static uint32_t w_screen_gen;
static int32_t w_master = 3000;                 /* Q12 */
static uint32_t fm1_ticks(void) { return (uint32_t)(wasm_frames * 544u); }   /* 24 MHz from the frames */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = w_pressed;
    *released = w_released;
    w_pressed = w_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t e = w_note_edges; w_note_edges = 0; return e; }
static int32_t fm1_enc_take(uint32_t e) { int32_t d = w_enc[e]; w_enc[e] = 0; return d; }
static void host_leds(const uint8_t *w) { memcpy(w_leds, w, 41); }

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
    w_screen_gen++;
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

/* USB-MIDI in from the page (Web MIDI) */
static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static uint8_t w_settings[260];
static int settings_load(void *p, uint32_t n) { if (!w_settings[0] || n > 256u) return -1; memcpy(p, w_settings + 4, n); return 0; }
static int settings_save(const void *p, uint32_t n) { w_settings[0] = 1; memcpy(w_settings + 4, p, n); return 0; }
static void set_lang(uint32_t l);
static void app_poll(void);
static void platform_poll(void) { app_poll(); }

static void app_audio(int32_t *out, uint32_t n);
static void app_init(void);
static void app_step(void);

static int32_t w_blk[2u * HALF_FRAMES];
static float w_out[2u * HALF_FRAMES];

WASM_EXPORT("kawa_init") void kawa_init(void) { app_init(); }
WASM_EXPORT("kawa_step") void kawa_step(void) { app_step(); }
WASM_EXPORT("kawa_audio") float *kawa_audio(void)   /* 256 frames, L R L R, -1..1 */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_FRAMES; i++)
        w_blk[i] = 0;
    app_audio(w_blk, HALF_FRAMES);
    for (i = 0; i < 2u * HALF_FRAMES; i++) {
        int32_t v = (w_blk[i] * w_master) >> 12;
        v = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
        w_out[i] = (float)v * (1.0f / 32768.0f);
    }
    wasm_frames += HALF_FRAMES;
    fm1_ms = (uint32_t)(wasm_frames * 1000u / FS);
    return w_out;
}
WASM_EXPORT("kawa_keys") void kawa_keys(uint32_t mask)
{
    w_note_edges |= mask & ~fm1_in.notes;
    fm1_in.notes = mask & 0x7FFFFFFu;
}
WASM_EXPORT("kawa_button") void kawa_button(uint32_t id, uint32_t down)
{
    if (id > 13u) return;
    if (down) { w_pressed |= 1u << id; fm1_in.buttons |= 1u << id; }
    else { w_released |= 1u << id; fm1_in.buttons &= ~(1u << id); }
}
WASM_EXPORT("kawa_turn") void kawa_turn(uint32_t enc, int32_t steps) { if (enc < 7u) w_enc[enc] += steps; }
WASM_EXPORT("kawa_master") void kawa_master(int32_t q12) { w_master = q12; }
WASM_EXPORT("kawa_midi") void kawa_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    mq[mq_w % 64u][0] = (uint8_t)st; mq[mq_w % 64u][1] = (uint8_t)d1; mq[mq_w % 64u][2] = (uint8_t)d2;
    mq_w++;
}
WASM_EXPORT("kawa_lang") void kawa_lang(uint32_t l) { set_lang(l); }   /* the page's language: FR 0, EN 1, ES 2 */
WASM_EXPORT("kawa_screen") uint16_t *kawa_screen(void) { return fb; }
WASM_EXPORT("kawa_screen_gen") uint32_t kawa_screen_gen(void) { return w_screen_gen; }
WASM_EXPORT("kawa_leds") uint8_t *kawa_leds(void) { return w_leds; }
