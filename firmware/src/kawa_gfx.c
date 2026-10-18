/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: drawing. The screen is drawn in six bands of 240 x 40: each band is drawn whole (everything
 * outside it is clipped), hashed, and sent only if it changed (12 MHz SPI: a full screen is ~77 ms).
 * Two send buffers take turns, so a band is drawn while the last one goes out. Colours are RGB565
 * in the drawing buffer, byte-swapped for the panel on the way out. */
#define BAND_H 40u
#define NBAND 6u
static uint16_t g_buf[240u * BAND_H] POOL;      /* the band being drawn */
static uint16_t g_txb[240u * BAND_H] POOL;      /* the second send buffer (the first: gfx.c's cv_px) */
static int32_t g_y0;                            /* the band's first row */
static uint32_t g_hash[NBAND];
static uint8_t g_force = 1, g_tx;

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
        for (i = x; i < x1; i++)
            p[i] = c;
    }
}

static void g_blend(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t t)
{
    int32_t x1 = x + w, y1 = y + h, i;
    if (x < 0) x = 0;
    if (x1 > 240) x1 = 240;
    if (y < g_y0) y = g_y0;
    if (y1 > g_y0 + (int32_t)BAND_H) y1 = g_y0 + (int32_t)BAND_H;
    for (; y < y1; y++) {
        uint16_t *p = &g_buf[(uint32_t)(y - g_y0) * 240u];
        for (i = x; i < x1; i++)
            p[i] = g_mix(p[i], c, t);
    }
}

static void g_px(int32_t x, int32_t y, uint16_t c)
{
    y -= g_y0;
    if ((uint32_t)x < 240u && (uint32_t)y < BAND_H)
        g_buf[(uint32_t)y * 240u + (uint32_t)x] = c;
}

static void g_rrect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* corners cut */
{
    if (!g_vis(y, h))
        return;
    g_rect(x + 1, y, w - 2, 1, c);
    g_rect(x, y + 1, w, h - 2, c);
    g_rect(x + 1, y + h - 1, w - 2, 1, c);
}

static void g_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)  /* a rounded outline */
{
    if (!g_vis(y, h))
        return;
    g_rect(x + 1, y, w - 2, 1, c);
    g_rect(x + 1, y + h - 1, w - 2, 1, c);
    g_rect(x, y + 1, 1, h - 2, c);
    g_rect(x + w - 1, y + 1, 1, h - 2, c);
}

static void g_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {
        int32_t e2 = 2 * err;
        g_px(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* text in FONT_S (8 x 16): T_CROP draws rows 2..13 only (12 px: capitals, digits), T_BOLD doubles the
 * strokes; the glyph's grey levels blend into what is under it. Returns the x after the text. */
enum { T_CROP = 1, T_BOLD = 2 };
static int32_t g_text(int32_t x, int32_t y, const char *s, uint16_t c, uint32_t mode)
{
    const felucca_font_t *f = &FONT_S;
    int32_t r0 = mode & T_CROP ? 2 : 0, r1 = mode & T_CROP ? 14 : 16;
    if (!g_vis(y, r1 - r0)) {
        while (*s) { x += 8; s++; }
        return x;
    }
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), w = f->bw[gi], bpr = (w + 1u) / 2u, gx;
        const uint8_t *gd = f->data + f->off[gi];
        int32_t gy;
        for (gy = r0; gy < r1; gy++) {
            int32_t py = y + gy - r0 - g_y0;
            uint16_t *row;
            if ((uint32_t)py >= BAND_H)
                continue;
            row = &g_buf[(uint32_t)py * 240u];
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[(uint32_t)gy * bpr + gx / 2u];
                int32_t px = x - (int32_t)f->pad + (int32_t)gx;
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (!v)
                    continue;
                if ((uint32_t)px < 240u)
                    row[px] = v >= 15u ? c : g_mix(row[px], c, v * 17u);
                if ((mode & T_BOLD) && (uint32_t)(px + 1) < 240u)
                    row[px + 1] = v >= 15u ? c : g_mix(row[px + 1], c, v * 17u);
            }
        }
        x += 8;
    }
    return x;
}
static int32_t g_text_w(const char *s) { return (int32_t)str_len(s) * 8; }
static void g_text_c(int32_t cx, int32_t y, const char *s, uint16_t c, uint32_t mode)   /* centred on cx */
{
    g_text(cx - g_text_w(s) / 2, y, s, c, mode);
}

/* FONT_L: 16 x 32 (capitals, digits) */
static int32_t g_text_l(int32_t x, int32_t y, const char *s, uint16_t c)
{
    const felucca_font_t *f = &FONT_L;
    if (!g_vis(y, 32)) {
        while (*s) { x += 16; s++; }
        return x;
    }
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), w = f->bw[gi], bpr = (w + 1u) / 2u, gx, gy;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++) {
            int32_t py = y + (int32_t)gy - g_y0;
            uint16_t *row;
            if ((uint32_t)py >= BAND_H)
                continue;
            row = &g_buf[(uint32_t)py * 240u];
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                int32_t px = x - (int32_t)f->pad + (int32_t)gx;
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v && (uint32_t)px < 240u)
                    row[px] = v >= 15u ? c : g_mix(row[px], c, v * 17u);
            }
        }
        x += f->adv[gi];
    }
    return x;
}

static void g_icon(int32_t x, int32_t y, uint32_t id, uint16_t c, uint32_t scale)
{
    uint32_t r, k;
    if (id >= ICO_N || !g_vis(y, (int32_t)(8u * scale)))
        return;
    for (r = 0; r < 8u; r++)
        for (k = 0; k < 8u; k++)
            if (ICONS[id][r] >> (7u - k) & 1u) {
                if (scale == 1u)
                    g_px(x + (int32_t)k, y + (int32_t)r, c);
                else
                    g_rect(x + (int32_t)(k * scale), y + (int32_t)(r * scale), (int32_t)scale, (int32_t)scale, c);
            }
}

/* a picture: w x h pixels, a byte each into pal (RGB565), at 1x (the boot's): the rows of this band only */
static void g_pic(int32_t x, int32_t y, const uint8_t *img, uint32_t w, uint32_t h, const uint16_t *pal)
{
    int32_t r = g_y0 - y, r1 = r + (int32_t)BAND_H, k0 = x < 0 ? -x : 0, k1 = x + (int32_t)w > 240 ? 240 - x : (int32_t)w, k;
    if (r < 0) r = 0;
    if (r1 > (int32_t)h) r1 = (int32_t)h;
    for (; r < r1; r++) {
        const uint8_t *s = img + (uint32_t)r * w;
        uint16_t *d = &g_buf[(uint32_t)(y + r - g_y0) * 240u + (uint32_t)x];
        for (k = k0; k < k1; k++)
            d[k] = pal[s[k]];
    }
}

/* the dog: w x h pixels at 4 bits (the low nibble is the left pixel), ink 0 clear, pal[ink] for the
 * others; flip mirrors it */
static void g_dog(int32_t x, int32_t y, const uint8_t *img, uint32_t w, uint32_t h, const uint16_t *pal,
                  uint32_t scale, uint32_t flip)
{
    uint32_t r, k;
    if (!g_vis(y, (int32_t)(h * scale)))
        return;
    for (r = 0; r < h; r++) {
        int32_t yy = y + (int32_t)(r * scale);
        if (!g_vis(yy, (int32_t)scale))
            continue;
        for (k = 0; k < w; k++) {
            uint32_t sk = flip ? w - 1u - k : k, b = img[(r * w + sk) >> 1], ink = sk & 1u ? b >> 4 : b & 15u;
            if (!ink)
                continue;
            if (scale == 1u)
                g_px(x + (int32_t)k, yy, pal[ink]);
            else
                g_rect(x + (int32_t)(k * scale), yy, (int32_t)scale, (int32_t)scale, pal[ink]);
        }
    }
}

/* a number into text (no printf on the chip) */
static char *fmt_u(char *p, uint32_t v, uint32_t digits)   /* digits 0: as many as needed */
{
    char t[12];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11u);
    while (n < digits) t[n++] = '0';
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static char *fmt_s(char *p, int32_t v)          /* with its sign */
{
    *p++ = v < 0 ? '-' : '+';
    return fmt_u(p, (uint32_t)(v < 0 ? -v : v), 0);
}
static char *str_cat(char *p, const char *s)   /* append s to the string at p; returns its new end */
{
    while (*p) p++;
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

/* one frame: draw(band) for each band, send what changed; poll in between (USB) */
static void g_frame_send(void (*draw)(void))
{
    uint32_t b;
    for (b = 0; b < NBAND; b++) {
        uint32_t h = 2166136261u, i, *w = (uint32_t *)g_buf;
        uint16_t *tx;
        g_y0 = (int32_t)(b * BAND_H);
        draw();
        for (i = 0; i < 240u * BAND_H / 2u; i++)
            h = (h ^ w[i]) * 16777619u;
        if (h == g_hash[b] && !g_force) {
            platform_poll();
            continue;
        }
        g_hash[b] = h;
        tx = g_tx ? g_txb : cv_px;              /* the other buffer than the one in flight (lcd_blit waits for */
        g_tx ^= 1u;                             /* the one before it: this one is free) */
        for (i = 0; i < 240u * BAND_H; i++) {
            uint16_t c = g_buf[i];
            tx[i] = (uint16_t)(c >> 8 | c << 8);
        }
        lcd_blit(0, b * BAND_H, 240, BAND_H, tx);
        platform_poll();
    }
    g_force = 0;
}
