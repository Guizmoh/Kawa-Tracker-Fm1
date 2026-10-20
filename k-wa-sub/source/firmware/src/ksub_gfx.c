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

/* the colours of the design */
#define K_BG RGB(11, 18, 20)
#define K_PANEL RGB(17, 27, 30)
#define K_LINE RGB(40, 56, 60)
#define K_LIME RGB(200, 240, 60)
#define K_LIMED RGB(30, 40, 20)
#define K_CYAN RGB(80, 216, 208)
#define K_WHITE RGB(236, 242, 242)
#define K_GREY RGB(130, 146, 148)
#define K_TRACK RGB(36, 52, 56)
#define K_ORANGE RGB(240, 140, 60)

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
