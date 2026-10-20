/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: drawing (as KAWA's): the screen is drawn in six bands of 240 x 40, each hashed and sent only if
 * it changed, two send buffers taking turns. Text: DejaVu Sans Bold in five sizes, 16 grey levels. */
#define BAND_H 40u
#define NBAND 6u
static uint16_t g_buf[240u * BAND_H] POOL;
static uint16_t g_txb[240u * BAND_H] POOL;
static int32_t g_y0;
static uint32_t g_hash[NBAND];
static uint8_t g_force = 1, g_tx;

/* the colours: six palettes for each theme (ksub_draw.c): its own, another, SYNTHWAVE, MINT, black and white, inverted */
enum { KC_BG, KC_PANEL, KC_LINE, KC_LIME, KC_LIMED, KC_CYAN, KC_WHITE, KC_GREY, KC_TRACK, KC_ORANGE, KC_WAVE, KC_INK, KC_MOD, KC_N };
#define C3(r, g, b) RGB(r, g, b)
static const uint16_t PALS[12][KC_N] = {
    /* CLASSIC: NEON, AMBER */
    {C3(11,18,20), C3(17,27,30), C3(40,56,60), C3(200,240,60), C3(30,40,20), C3(80,216,208), C3(236,242,242), C3(130,146,148), C3(36,52,56), C3(240,140,60), C3(200,240,60), C3(11,18,20), C3(170,140,250)},
    {C3(18,12,6), C3(30,20,10), C3(66,44,20), C3(255,170,40), C3(52,34,10), C3(255,214,140), C3(250,238,220), C3(170,140,110), C3(60,40,20), C3(255,90,60), C3(255,170,40), C3(18,12,6), C3(255,214,140)},
    /* STUDIO: STUDIO, OCEAN */
    {C3(10,14,16), C3(22,30,33), C3(44,58,62), C3(200,240,60), C3(30,40,20), C3(80,216,208), C3(240,244,244), C3(140,156,158), C3(44,58,62), C3(240,140,60), C3(200,240,60), C3(10,14,16), C3(170,140,250)},
    {C3(8,14,30), C3(16,26,50), C3(36,54,90), C3(80,200,255), C3(16,40,70), C3(255,120,200), C3(236,244,255), C3(130,150,180), C3(36,54,90), C3(255,170,60), C3(80,200,255), C3(8,14,30), C3(255,120,200)},
    /* PURE: PURE, PURE RED */
    {C3(28,28,32), C3(42,42,47), C3(62,62,70), C3(204,240,58), C3(50,50,58), C3(160,132,248), C3(238,238,242), C3(150,150,162), C3(60,60,68), C3(240,150,70), C3(160,132,248), C3(20,20,22), C3(160,132,248)},
    {C3(28,26,28), C3(44,40,42), C3(70,62,64), C3(255,90,70), C3(60,36,34), C3(255,190,120), C3(244,238,236), C3(160,150,150), C3(70,62,64), C3(255,200,60), C3(255,190,120), C3(24,20,20), C3(255,190,120)},
    /* RACK: RACK, MAGMA */
    {C3(8,10,12), C3(20,24,27), C3(40,46,50), C3(210,245,70), C3(32,42,16), C3(90,220,210), C3(250,250,250), C3(150,160,164), C3(40,46,50), C3(240,140,60), C3(90,220,210), C3(8,10,12), C3(175,145,255)},
    {C3(10,8,8), C3(26,18,16), C3(56,36,30), C3(255,120,40), C3(50,24,12), C3(255,214,80), C3(255,246,236), C3(170,140,120), C3(56,36,30), C3(255,60,60), C3(255,214,80), C3(10,8,8), C3(255,170,120)},
    /* for every theme: SYNTHWAVE, MINT */
    {C3(18,8,30), C3(32,16,52), C3(70,40,100), C3(255,70,200), C3(60,20,70), C3(80,230,255), C3(250,236,255), C3(170,140,200), C3(64,36,90), C3(255,160,60), C3(255,70,200), C3(18,8,30), C3(80,230,255)},
    {C3(8,20,16), C3(14,34,28), C3(34,70,58), C3(120,255,190), C3(20,60,44), C3(255,230,140), C3(230,255,244), C3(120,170,150), C3(30,64,52), C3(255,140,90), C3(120,255,190), C3(8,20,16), C3(255,230,140)},
    /* MONO: white on black (classic); INVERT: greys, dark on light, the cards a shade darker (as through a tint) */
    {C3(0,0,0), C3(16,16,16), C3(70,70,70), C3(255,255,255), C3(48,48,48), C3(200,200,200), C3(255,255,255), C3(140,140,140), C3(50,50,50), C3(255,255,255), C3(255,255,255), C3(0,0,0), C3(200,200,200)},
    {C3(206,209,212), C3(184,188,192), C3(156,160,165), C3(34,38,44), C3(160,164,170), C3(74,80,88), C3(22,24,28), C3(92,98,104), C3(166,170,175), C3(52,56,62), C3(40,44,50), C3(222,224,226), C3(100,104,116)},
};
static const char *const PAL_NAME[4][2] = {{"NEON", "AMBER"}, {"STUDIO", "OCEAN"}, {"PURE", "PURE RED"}, {"RACK", "MAGMA"}};
static uint16_t kc[KC_N];
static uint8_t theme, theme_pal;
static void theme_set(uint32_t t, uint32_t pal)
{
    uint32_t i, k;
    theme = (uint8_t)(t < 4u ? t : 0u);
    theme_pal = (uint8_t)(pal < 6u ? pal : 0u);
    k = theme_pal < 2u ? theme * 2u + theme_pal : 6u + theme_pal;
    for (i = 0; i < KC_N; i++) kc[i] = PALS[k][i];
}
#define K_BG kc[KC_BG]
#define K_PANEL kc[KC_PANEL]
#define K_LINE kc[KC_LINE]
#define K_LIME kc[KC_LIME]
#define K_LIMED kc[KC_LIMED]
#define K_CYAN kc[KC_CYAN]
#define K_WHITE kc[KC_WHITE]
#define K_GREY kc[KC_GREY]
#define K_TRACK kc[KC_TRACK]
#define K_ORANGE kc[KC_ORANGE]
#define K_WAVE kc[KC_WAVE]
#define K_INK kc[KC_INK]
#define K_MOD kc[KC_MOD]

/* the fonts: four families (SANS DejaVu Sans Bold, MONO IBM Plex Mono, HYPER Atkinson Hyperlegible Mono, PIXEL
 * Press Start 2P), five sizes */
static const ks_font_t *const FAMS[4][5] = {{&F_XS, &F_S, &F_M, &F_B, &F_L}, {&F_GXS, &F_GS, &F_GM, &F_GB, &F_GL},
                                           {&F_HXS, &F_HS, &F_HM, &F_HB, &F_HL}, {&F_PXS, &F_PS, &F_PM, &F_PB, &F_PL}};
static uint8_t g_bold;                          /* (HYPER is light: drawn twice, a pixel apart) */
static const ks_font_t *tf[5] = {&F_XS, &F_S, &F_M, &F_B, &F_L};
static void font_set(uint32_t f) { uint32_t i; for (i = 0; i < 5u; i++) tf[i] = FAMS[f & 3u][i]; g_bold = (f & 3u) == 2u; }
#define T_XS tf[0]
#define T_S tf[1]
#define T_M tf[2]
#define T_B tf[3]
#define T_L tf[4]

static int g_vis(int32_t y, int32_t h) { return y < g_y0 + (int32_t)BAND_H && y + h > g_y0; }
static uint16_t g_mix(uint16_t a, uint16_t b, uint32_t t)   /* a .. b, t = 0..256 */
{
    uint32_t ar = a >> 11, ag = (a >> 5) & 63u, ab = a & 31u, br = b >> 11, bg = (b >> 5) & 63u, bb = b & 31u;
    ar = (ar * (256u - t) + br * t) >> 8;
    ag = (ag * (256u - t) + bg * t) >> 8;
    ab = (ab * (256u - t) + bb * t) >> 8;
    return (uint16_t)(ar << 11 | ag << 5 | ab);
}
static void g_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h, i;
    if (x < 0) x = 0;
    if (x1 > 240) x1 = 240;
    if (y < g_y0) y = g_y0;
    if (y1 > g_y0 + (int32_t)BAND_H) y1 = g_y0 + (int32_t)BAND_H;
    for (; y < y1; y++) {
        uint16_t *p = &g_buf[(uint32_t)(y - g_y0) * 240u];
        for (i = x; i < x1; i++) p[i] = c;
    }
}
static void g_px(int32_t x, int32_t y, uint16_t c)
{
    y -= g_y0;
    if ((uint32_t)x < 240u && (uint32_t)y < BAND_H) g_buf[(uint32_t)y * 240u + (uint32_t)x] = c;
}
static void g_pxa(int32_t x, int32_t y, uint16_t c, uint32_t t)   /* a pixel blended in (t 0..256) */
{
    y -= g_y0;
    if ((uint32_t)x < 240u && (uint32_t)y < BAND_H) {
        uint16_t *p = &g_buf[(uint32_t)y * 240u + (uint32_t)x];
        *p = g_mix(*p, c, t);
    }
}
static void g_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill, uint16_t edge)   /* rounded, 1 px edge */
{
    if (!g_vis(y, h)) return;
    g_rect(x + 1, y, w - 2, h, edge);
    g_rect(x, y + 1, w, h - 2, edge);
    g_rect(x + 1, y + 1, w - 2, h - 2, fill);
    g_px(x + 1, y + 1, edge);
    g_px(x + w - 2, y + 1, edge);
    g_px(x + 1, y + h - 2, edge);
    g_px(x + w - 2, y + h - 2, edge);
}
/* a setting's bar: the part up to the knob a little softer, the knob's place a sharp line, a light fade on each side */
static void g_vbar(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t frac, uint16_t c)   /* frac 0..256 */
{
    int32_t f = (int32_t)((uint32_t)(w - 2) * frac >> 8), i;
    uint16_t soft = g_mix(K_TRACK, c, 150);
    if (!g_vis(y, h)) return;
    g_rect(x, y, w, h, K_TRACK);
    if (f > 0) g_rect(x, y, f, h, soft);
    for (i = 1; i <= 6; i++) {
        uint32_t t = 256u - (uint32_t)i * 34u;
        if (f - i >= 0) g_rect(x + f - i, y, 1, h, g_mix(soft, c, t));
        if (f + 1 + i < w) g_rect(x + f + 1 + i, y, 1, h, g_mix(K_TRACK, c, t * 3u / 4u));
    }
    g_rect(x + f, y, 2, h, c);
    if (h > 3) { g_px(x, y, K_BG); g_px(x, y + h - 1, K_BG); g_px(x + w - 1, y, K_BG); g_px(x + w - 1, y + h - 1, K_BG); }
}
static void g_bar(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t frac, uint16_t c)   /* frac 0..256 */
{
    int32_t f = (int32_t)((uint32_t)w * frac >> 8);
    if (!g_vis(y, h)) return;
    g_rect(x, y, w, h, K_TRACK);
    if (f > 0) g_rect(x, y, f, h, c);
    if (h > 3) {                                /* rounded ends */
        g_px(x, y, K_BG); g_px(x, y + h - 1, K_BG); g_px(x + w - 1, y, K_BG); g_px(x + w - 1, y + h - 1, K_BG);
    }
}
static void g_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* (anti-aliased in y) */
{
    int32_t dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy), i;
    if (!n) { g_px(x0, y0, c); return; }
    if ((y0 < g_y0 - 1 && y1 < g_y0 - 1) || (y0 > g_y0 + (int32_t)BAND_H && y1 > g_y0 + (int32_t)BAND_H)) return;
    for (i = 0; i <= n; i++) {
        int32_t x = x0 + dx * i / n, y8 = (y0 << 8) + (dy << 8) * i / n, fy = y8 & 255;
        g_pxa(x, y8 >> 8, c, 256u - (uint32_t)fy);
        g_pxa(x, (y8 >> 8) + 1, c, (uint32_t)fy);
    }
}

static int32_t g_text(const ks_font_t *f, int32_t x, int32_t y, const char *s, uint16_t c)
{
    int vis = g_vis(y, f->h);
    for (; *s; s++) {
        uint32_t ch = (uint8_t)*s, w, bpr, gx, gy;
        const uint8_t *d;
        if (ch == 0xB7) ch = f->last;           /* the middle dot */
        if (ch >= 'a' && ch <= 'z' && f->last < 'a') ch -= 32u;
        if (ch < f->first || ch > f->last) ch = '?';
        w = f->adv[ch - f->first];
        if (vis) {
            bpr = (w + 1u) / 2u;
            d = f->data + f->off[ch - f->first];
            for (gy = 0; gy < f->h; gy++) {
                int32_t py = y + (int32_t)gy - g_y0;
                uint16_t *row;
                if ((uint32_t)py >= BAND_H) continue;
                row = &g_buf[(uint32_t)py * 240u];
                for (gx = 0; gx < w; gx++) {
                    uint32_t v = d[gy * bpr + gx / 2u];
                    int32_t px = x + (int32_t)gx;
                    v = (gx & 1u) ? (v & 15u) : (v >> 4);
                    if (v && (uint32_t)px < 240u) row[px] = v >= 15u ? c : g_mix(row[px], c, v * 17u);
                    if (v && g_bold && (uint32_t)(px + 1) < 240u) row[px + 1] = v >= 15u ? c : g_mix(row[px + 1], c, v * 17u);
                }
            }
        }
        x += (int32_t)w;
    }
    return x;
}
static int32_t g_text_w(const ks_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++) {
        uint32_t ch = (uint8_t)*s;
        if (ch == 0xB7) ch = f->last;
        if (ch >= 'a' && ch <= 'z' && f->last < 'a') ch -= 32u;
        if (ch < f->first || ch > f->last) ch = '?';
        w += f->adv[ch - f->first];
    }
    return w;
}
static void g_text_r(const ks_font_t *f, int32_t xr, int32_t y, const char *s, uint16_t c) { g_text(f, xr - g_text_w(f, s), y, s, c); }
static void g_text_c(const ks_font_t *f, int32_t xc, int32_t y, const char *s, uint16_t c) { g_text(f, xc - g_text_w(f, s) / 2, y, s, c); }

static char *str_cat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *fmt_u(char *p, uint32_t v, uint32_t digits)
{
    char t[12];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v || n < digits);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static char *fmt_s(char *p, int32_t v)          /* with its sign */
{
    *p++ = v < 0 ? '-' : '+';
    return fmt_u(p, (uint32_t)(v < 0 ? -v : v), 0);
}

/* one frame: draw() for each band, send what changed; poll in between (USB) */
static void g_frame_send(void (*draw)(void))
{
    uint32_t b;
    for (b = 0; b < NBAND; b++) {
        uint32_t h = 2166136261u, i, *w = (uint32_t *)g_buf;
        uint16_t *tx;
        g_y0 = (int32_t)(b * BAND_H);
        draw();
        for (i = 0; i < 240u * BAND_H / 2u; i++) h = (h ^ w[i]) * 16777619u;
        if (h == g_hash[b] && !g_force) { platform_poll(); continue; }
        g_hash[b] = h;
        tx = g_tx ? g_txb : cv_px;
        g_tx ^= 1u;
        for (i = 0; i < 240u * BAND_H; i++) { uint16_t c = g_buf[i]; tx[i] = (uint16_t)(c >> 8 | c << 8); }
        lcd_blit(0, b * BAND_H, 240, BAND_H, tx);
        platform_poll();
    }
    g_force = 0;
}
