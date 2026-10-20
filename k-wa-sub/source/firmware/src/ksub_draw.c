/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: what the screen shows. Four themes share the same visuals and the same items:
 *   CLASSIC  the first design: banner, title, a visual, rows or boxes
 *   STUDIO   no banner: a status bar and the eight pages as tabs, the items as a grid of cells (the knob's colour,
 *            what modulates it), a wide visual
 *   PURE     after Pure SUB: a lime tab, a big visual, four rings for the four knobs, the row being edited below
 *   RACK     the pages on a rail at the left, big type, tall rows
 * Every visual is drawn with integers only (no FPU), from the patch and what the voices say (ksub_dsp.c). */
enum { TH_CLASSIC, TH_STUDIO, TH_PURE, TH_RACK, NTHEMES };
enum { VZ_NONE, VZ_SCOPE, VZ_OSCA, VZ_OSCB, VZ_SUB, VZ_MIX, VZ_ENV0, VZ_ENV1, VZ_ENV2, VZ_FILT, VZ_LFO1, VZ_LFO2, VZ_STEP,
       VZ_DRIVE, VZ_CRUSH, VZ_CHORUS, VZ_DELAY, VZ_REVERB, VZ_MASTER, VZ_CPU };
static const uint8_t VZ_OF[] = {VZ_SCOPE, VZ_SCOPE, VZ_OSCA, VZ_OSCB, VZ_SUB, VZ_MIX, VZ_ENV0, VZ_ENV1, VZ_ENV2, VZ_FILT, VZ_FILT,
                                VZ_LFO1, VZ_LFO2, VZ_STEP, VZ_NONE, VZ_DRIVE, VZ_CRUSH, VZ_CHORUS, VZ_DELAY, VZ_REVERB,
                                VZ_MASTER, VZ_NONE, VZ_NONE, VZ_NONE, VZ_CPU};
static const char *const PAGE_SHORT[NPAGE] = {"PLAY", "BASS", "ENV", "FILT", "LFO", "FX", "PRE", "SYS"};
static const uint16_t KNOB_RGB[4] = {RGB(255, 92, 92), RGB(255, 204, 64), RGB(92, 226, 116), RGB(84, 164, 255)};
static int16_t live[224];

/* ---------------------------------------------------------------- small pieces */
static void draw_poly(int32_t x, int32_t y, int32_t w, int32_t h, const int16_t *pt, uint32_t n, uint16_t c)   /* Q15 points */
{
    uint32_t i;
    int32_t cy = y + h / 2, px = x, py = cy - (pt[0] * (h / 2 - 1)) / 32768;
    if (!g_vis(y - 1, h + 2)) return;
    for (i = 1; i < n; i++) {
        int32_t xx = x + (int32_t)(i * (uint32_t)(w - 1) / (n - 1u)), yy = cy - (pt[i] * (h / 2 - 1)) / 32768;
        g_line(px, py, xx, yy, c);
        g_line(px, py + 1, xx, yy + 1, g_mix(K_BG, c, 120));
        px = xx;
        py = yy;
    }
}
static void g_col(int32_t x, int32_t ytop, int32_t ybot, uint16_t fill, uint16_t edge)   /* a filled column with a lit top */
{
    if (ytop < ybot) g_rect(x, ytop, 1, ybot - ytop, fill);
    g_px(x, ytop, edge);
    g_px(x, ytop + 1, edge);
}
static void g_ring(int32_t cx, int32_t cy, int32_t r, uint32_t frac, uint16_t c)   /* 270 degrees, frac 0..256 */
{
    uint32_t i, n = 90;
    if (!g_vis(cy - r - 1, 2 * r + 3)) return;
    for (i = 0; i <= n; i++) {
        uint32_t a = 0x60000000u + i * (0xC0000000u / n), k;
        uint16_t col = i * 256u <= frac * n ? c : K_TRACK;
        for (k = 0; k < 3u; k++) {
            int32_t rr = r - (int32_t)k;
            g_px(cx + ((sin32(a + 0x40000000u) * rr) >> 15), cy + ((sin32(a) * rr) >> 15), col);
        }
    }
    {
        uint32_t a = 0x60000000u + (uint32_t)(((uint64_t)0xC0000000u * frac) >> 8);
        g_line(cx, cy, cx + ((sin32(a + 0x40000000u) * (r - 6)) >> 15), cy + ((sin32(a) * (r - 6)) >> 15), K_WHITE);
    }
}
static int32_t dest_of(uint32_t id)             /* the destination that moves this parameter (-1: none) */
{
    switch (id) {
    case F_CUT: return D_CUT; case F_RES: return D_RES; case F_DRV: return D_DRV;
    case A_LVL: return D_ALVL; case A_P1: return D_AP1; case A_P2: return D_AP2; case A_P3: return D_AP3;
    case A_OCT: case A_SEMI: case A_FINE: return D_APIT;
    case B_LVL: return D_BLVL; case B_P1: return D_BP1; case B_P2: return D_BP2; case B_P3: return D_BP3;
    case B_OCT: case B_SEMI: case B_FINE: return D_BPIT;
    case X_AMT: return D_XMOD; case S_LVL: return D_SUB; case N_LVL: return D_NOISE; case N_COL: return D_NCOL;
    case L1_DEP: return D_WOB;
    case F_ENV: return D_FENV; case L2_DEP: return D_L2DEP; case L1_RATE: case L1_DIV: return D_L1RATE;
    case L2_RATE: case L2_DIV: return D_L2RATE; case ST_GATE: return D_GATE;
    case DR_AMT: return D_DRAMT; case DR_MIX: return D_DRMIX; case CR_BITS: return D_CRBITS; case CR_RATE: return D_CRRATE;
    case CR_MIX: return D_CRMIX; case CH_RATE: return D_CHRATE; case CH_DEP: return D_CHDEP; case CH_MIX: return D_CHMIX;
    case DL_FB: return D_DLFB; case DL_TONE: return D_DLTONE; case DL_MIX: return D_DLMIX; case RV_SIZE: return D_RVSIZE;
    case RV_DAMP: return D_RVDAMP; case RV_MIX: return D_RVMIX; case MS_GLUE: return D_GLUE; case MS_SUB: return D_SUBB;
    case MS_VOL: return D_VOL; case I_L1RATE: return D_L1RATE; case I_L2RATE: return D_L2RATE;
    default: return -1;
    }
}
static const char *mod_tag(uint32_t id)         /* what modulates it, in two or three letters */
{
    int32_t d = dest_of(id), k;
    if (d < 0) return 0;
    if (patch[L1_DEP] && patch[L1_DST] == d) return "L1";
    if (patch[L2_DEP] && patch[L2_DST] == d) return "L2";
    if (patch[ST_ON] && patch[ST_DEP] && patch[ST_DST] == d) return "STP";
    if ((patch[ME_AMT] && patch[ME_DST] == d) || (d == D_CUT && patch[F_ENV])) return "ENV";
    for (k = 0; k < 4; k++) if (patch[M1_SRC + 3 * k] && patch[M1_AMT + 3 * k] && patch[M1_DST + 3 * k] == d) return "MX";
    return 0;
}
static int32_t knob_of(const view_t *v, uint32_t id)
{
    int32_t k;
    for (k = 0; k < 4; k++) if (v->k[k] == id) return k;
    return -1;
}

/* ---------------------------------------------------------------- the visuals */
static void vis_env(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t k)
{
    static const uint8_t PA[3][4] = {{AE_A, AE_D, AE_S, AE_R}, {FE_A, FE_D, FE_S, FE_R}, {ME_A, ME_D, 0, ME_D}};
    int32_t a = patch[PA[k][0]], d = patch[PA[k][1]], s = k == 2u ? 0 : patch[PA[k][2]] * 258, r = patch[PA[k][3]];
    int32_t wa = 2 + a, wd = 2 + d, ws = k == 2u ? 0 : 40, wr = k == 2u ? 0 : 2 + r, tot = wa + wd + ws + wr, W = w - 10;
    int32_t x0 = x + 5, xa, xd, xs, xr, y0 = y + h - 4, H = h - 9, px, st = env_st_show[k], lv = env_lv_show[k];
    uint16_t fill = g_mix(K_PANEL, K_WAVE, 70);
    xa = x0 + wa * W / tot; xd = xa + wd * W / tot; xs = xd + ws * W / tot; xr = xs + wr * W / tot;
    if (!g_vis(y, h)) return;
    for (px = x0; px <= xr; px++) {
        int32_t l, t;
        if (px <= xa) l = xa > x0 ? 32767 * (px - x0) / (xa - x0) : 32767;
        else if (px <= xd) { t = 32767 - 32767 * (px - xa) / (xd - xa + 1); l = s + mul15(32767 - s, mul15(t, t)); }
        else if (px <= xs) l = s;
        else { t = 32767 - 32767 * (px - xs) / (xr - xs + 1); l = mul15(s, mul15(t, t)); }
        g_col(px, y0 - l * H / 32767, y0, fill, K_WAVE);
    }
    g_rect(x0, y0, W, 1, K_LINE);
    if (st) {                                   /* where the newest note is on it */
        int32_t dx;
        if (st == 1) dx = x0 + (xa - x0) * lv / 32767;
        else if (st == 2) dx = xa + (xd - xa) * (32767 - lv) / (32767 - s + 1);
        else if (st == 3) dx = xd + (xs - xd) / 2;
        else dx = xs + (xr - xs) * (s ? clampi(32767 - lv * 32767 / (s + 1), 0, 32767) : 32767 - lv) / 32767;
        dx = clampi(dx, x0, xr);
        g_rect(dx - 3, y0 - lv * H / 32767 - 3, 7, 7, K_LIME);
        g_rect(dx - 1, y0 - lv * H / 32767 - 1, 3, 3, K_INK);
    }
}
static void vis_filter(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t ic = patch[F_CUT] * 2, res = patch[F_RES], ft = patch[F_TYPE], px, W = w - 10, top = y + 3, H = h - 7;
    int32_t peak = res * 15 * 256 / 127, fc = KS_CUT_HZ[ic];
    uint16_t fill = g_mix(K_PANEL, K_WAVE, 70);
    if (!g_vis(y, h)) return;
    for (px = 0; px < W; px++) {
        int32_t i = px * 255 / (W - 1), o = (i - ic) * 256 / 26, ao = o < 0 ? -o : o, db = 0, b;   /* o: octaves Q8, db Q8 */
        b = ao < 154 ? peak - peak * ao / 154 : 0;   /* the resonance, over 0.6 octave */
        switch (ft) {
        case FT_LP24: db = o > 0 ? -24 * o : 0; db += b; break;
        case FT_LP12: db = o > 0 ? -12 * o : 0; db += b; break;
        case FT_HP: db = o < 0 ? 24 * o : 0; db += b; break;
        case FT_BP: db = -12 * ao + b; break;
        case FT_NOTCH: db = ao < 77 ? -36 * 256 + 36 * 256 * ao / 77 : 0; break;
        case FT_VOWEL: {                        /* the three formants of the vowel the cutoff points at */
            uint32_t vw = (uint32_t)q8to15(patch[F_CUT] * 256) * 4u, vi = vw >> 15, k;
            int32_t hz = KS_CUT_HZ[i];
            if (vi > 3u) vi = 3u;
            db = -18 * 256;
            for (k = 0; k < 3u; k++) {
                int32_t f = VOX_F[vi][k], d = hz > f ? hz - f : f - hz, bw = f / 4 + 60;
                int32_t pk = -18 * 256 + (18 * 256 + peak) * (bw - d) / bw - (int32_t)k * 6 * 256;
                if (d < bw && pk > db) db = pk;
            }
            break;
        }
        default: {                              /* COMB: peaks every cutoff Hz */
            db = (sin32((uint32_t)((uint32_t)KS_CUT_HZ[i] * (0xFFFFFFFFu / (uint32_t)(fc ? fc : 1))) + 0x40000000u) * (6 + res / 8)) >> 7;
            break;
        }
        }
        db = clampi(db, -36 * 256, 18 * 256);
        g_col(x + 5 + px, top + (18 * 256 - db) * H / (54 * 256), y + h - 4, fill, K_WAVE);
    }
    {
        int32_t xc = x + 5 + ic * (W - 1) / 255, xl = x + 5 + (cut_show >> 7) * (W - 1) / 255;
        g_rect(xc, top, 1, H, K_LINE);
        if (ks_sounding) { g_rect(xl - 3, top + 2, 7, 7, K_LIME); g_rect(xl - 1, top + 4, 3, 3, K_INK); }
    }
}
static void vis_lfo(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t k)
{
    int16_t pt[112];
    uint32_t i, ph = lfo_ph_show[k];
    lfo_t l;
    if (!g_vis(y, h)) return;
    memset(&l, 0, sizeof l);
    l.sh = 20000; l.sh0 = -12000;
    for (i = 0; i < 112u; i++) pt[i] = (int16_t)lfo_shape(&l, i * (0xFFFFFFFFu / 56u), (uint32_t)patch[L1_SHP + k * LFO_2]);
    g_rect(x + 4, y + h / 2, w - 8, 1, K_LINE);
    draw_poly(x + 4, y + 2, w - 8, h - 4, pt, 112, K_WAVE);
    {
        int32_t dx = x + 4 + (int32_t)((ph >> 24) * (uint32_t)(w - 9) / 510u), dy = y + h / 2 - (lfo[k].out * (h / 2 - 3)) / 32768;
        g_rect(dx - 3, dy - 3, 7, 7, K_LIME);
    }
}
static void vis_step(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t i, bw = (w - 8) / 16, x0 = x + 4 + (w - 8 - bw * 16) / 2, H = h - 8;
    if (!g_vis(y, h)) return;
    for (i = 0; i < 16; i++) {
        int32_t lv;
        char c = STEP_PAT[patch[ST_PAT] & 15][i];
        lv = patch[ST_PAT] == 15 ? (int32_t)(((uint32_t)i * 2654435761u) >> 28) : c <= '9' ? c - '0' : c - 'A' + 10;
        g_rect(x0 + i * bw + 1, y + 4, bw - 2, H, K_TRACK);
        g_rect(x0 + i * bw + 1, y + 4 + H - lv * H / 15, bw - 2, lv * H / 15,
               !patch[ST_ON] ? K_LINE : i == step_pos ? K_LIME : (i & 3) ? K_WAVE : g_mix(K_WAVE, K_WHITE, 90));
    }
}
static void vis_fx(uint32_t kind, int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t px, W = w - 10, x0 = x + 5, cy = y + h / 2, H2 = h / 2 - 3;
    uint16_t fill = g_mix(K_PANEL, K_WAVE, 70);
    if (!g_vis(y, h)) return;
    switch (kind) {
    case VZ_DRIVE: {                            /* the transfer curve: in -> out */
        int32_t g = 256 + patch[DR_AMT] * patch[DR_AMT] * 3 / 4;
        g_line(x0, cy + H2, x0 + W, cy - H2, K_LINE);
        for (px = 0; px < W; px++) {
            int32_t in = (px * 65534 / (W - 1)) - 32767, o = (int32_t)(((int64_t)in * g) >> 8);
            switch (patch[DR_TYPE]) {
            case 0: o = sat15(o); break;
            case 1: o = sat15(o + (int32_t)(((int64_t)o * o) >> 17)); break;
            case 2: o = clip(o, 26000) * 5 / 4; break;
            default: o = lfold(o); break;
            }
            g_px(x0 + px, cy - o * H2 / 32768, K_WAVE);
            g_px(x0 + px, cy - o * H2 / 32768 + 1, K_WAVE);
        }
        break;
    }
    case VZ_CRUSH: {                            /* one cycle, held and stepped */
        int32_t bits = 16 - ((patch[CR_BITS] * 14) >> 7), hold = 1 + ((patch[CR_RATE] * 31) >> 7), lv = 1 << (bits > 5 ? 5 : bits - 1);
        for (px = 0; px < W; px++) {
            int32_t q = px - px % (hold > 1 ? hold : 1), s = sin32((uint32_t)q * (0xFFFFFFFFu / (uint32_t)W));
            s = (s * lv / 32768) * 32767 / lv;
            g_col(x0 + px, cy - s * H2 / 32768, cy, fill, K_WAVE);
        }
        break;
    }
    case VZ_CHORUS: {                           /* left and right, sliding against each other */
        uint32_t t = fm1_ms * (uint32_t)(1 + patch[CH_RATE]) * 40000u;
        int32_t dep = patch[CH_DEP] * 40;
        for (px = 0; px < W; px++) {
            uint32_t ph = (uint32_t)px * (0xFFFFFFFFu / (uint32_t)W) * 2u;
            g_px(x0 + px, cy - sin32(ph + (uint32_t)((sin32(t) * dep) >> 4)) * H2 / 32768, K_WAVE);
            g_px(x0 + px, cy - sin32(ph - (uint32_t)((sin32(t) * dep) >> 4)) * H2 / 32768, K_CYAN);
        }
        break;
    }
    case VZ_DELAY: {                            /* the repeats: spacing by the time, height by the feedback */
        int32_t sp = 6 + (patch[DL_TIME] - 3) * 3, a = 32767, k = 0;
        for (px = x0; px < x0 + W && a > 600; px += sp, k++) {
            int32_t hh = a * (h - 8) / 32767;
            g_rect(px, y + h - 4 - hh, 4, hh, k == 0 ? K_WHITE : (k & 1) ? K_WAVE : K_CYAN);
            a = k == 0 ? a * 3 / 4 : mul15(a, patch[DL_FB] * 250);
        }
        break;
    }
    case VZ_REVERB: {                           /* the tail: dense and falling */
        int32_t a = 32767, dec = 32767 - (127 - patch[RV_SIZE]) * 6 - 40;
        for (px = 0; px < W; px++) {
            uint32_t hsh = (uint32_t)px * 2654435761u;
            int32_t r = (int32_t)((hsh >> 24) & 255u) * a >> 8;
            g_col(x0 + px, y + h - 4 - r * (h - 8) / 32767, y + h - 4, fill, K_WAVE);
            a = mul15(a, dec);
        }
        break;
    }
    case VZ_MASTER: {                           /* the output's peak and what the glue takes */
        int32_t pk = clampi(meter_pk, 0, 32767), gr = 32767 - clampi(meter_gr, 0, 32767), bw = W - 30;
        g_text(T_XS, x0, y + 6, "OUT", K_GREY);
        g_bar(x0 + 28, y + 7, bw, 6, (uint32_t)(pk >> 7), pk > 30000 ? K_ORANGE : K_WAVE);
        g_text(T_XS, x0, y + h - 14, "GLUE", K_GREY);
        g_bar(x0 + 28, y + h - 13, bw, 6, (uint32_t)(gr >> 6), K_CYAN);
        break;
    }
    default: {                                  /* CPU: each core's share of a block */
        g_text(T_XS, x0, y + 6, "C0", K_GREY);
        g_bar(x0 + 20, y + 7, W - 20, 6, ks_load[0] * 256u / 100u, ks_load[0] > 70u ? K_ORANGE : K_WAVE);
        g_text(T_XS, x0, y + h - 14, "C1", K_GREY);
        g_bar(x0 + 20, y + h - 13, W - 20, 6, dual_on ? ks_load[1] * 256u / 100u : 0u, K_CYAN);
        break;
    }
    }
}
static void draw_visual(uint32_t kind, int32_t x, int32_t y, int32_t w, int32_t h, int box)
{
    if (!kind || !g_vis(y, h)) return;
    if (box) g_box(x, y, w, h, K_PANEL, K_LINE);
    switch (kind) {
    case VZ_SCOPE:
        if (ks_sounding) {                      /* the last ~20 ms of sound, from a rising zero crossing */
            uint32_t sw = scope_w - 512u, k, at = 0;
            for (k = 1; k < 288u; k++) if (scope[(sw + k - 1u) & 511u] < 0 && scope[(sw + k) & 511u] >= 0) { at = k; break; }
            for (k = 0; k < 224u; k++) live[k] = scope[(sw + at + k) & 511u];
            draw_poly(x + 3, y + 2, w - 6, h - 4, live, 224, K_WAVE);
        } else
            draw_poly(x + 3, y + 2, w - 6, h - 4, prev_wave, 224, K_WAVE);
        break;
    case VZ_OSCA: draw_poly(x + 3, y + 2, w - 6, h - 4, prev_a, 112, K_WAVE); break;
    case VZ_OSCB: draw_poly(x + 3, y + 2, w - 6, h - 4, prev_b, 112, K_WAVE); break;
    case VZ_SUB: draw_poly(x + 3, y + 2, w - 6, h - 4, prev_s, 112, K_WAVE); break;
    case VZ_MIX: draw_poly(x + 3, y + 2, w - 6, h - 4, prev_wave, 224, K_WAVE); break;
    case VZ_ENV0: case VZ_ENV1: case VZ_ENV2: vis_env(x, y, w, h, kind - VZ_ENV0); break;
    case VZ_FILT: vis_filter(x, y, w, h); break;
    case VZ_LFO1: case VZ_LFO2: vis_lfo(x, y, w, h, kind - VZ_LFO1); break;
    case VZ_STEP: vis_step(x, y, w, h); break;
    default: vis_fx(kind, x, y, w, h); break;
    }
}

/* ---------------------------------------------------------------- the titles */
static const char *view_title(uint32_t vi, char *t, char *sub)
{
    const view_t *v = &VIEWS[vi];
    char *p;
    *sub = 0;
    switch (v->lay) {
    case LY_PLAY:
        p = str_cat(sub, L_STYLE[clampi(patch[P_STYLE], 0, NSTYLE - 1)]);
        p = str_cat(p, " \xB7 ");
        p = eng_str(p, patch);
        p = str_cat(p, set.bank ? " \xB7 B" : " \xB7 A");
        fmt_u(p, (uint32_t)set.idx + 1u, 2);
        return pname;
    case LY_PRESET:
        p = str_cat(sub, vi == PAGE_V0[PG_PRE] ? "FACTORY \xB7 " : "USER \xB7 ");
        p = fmt_u(p, vi == PAGE_V0[PG_PRE] ? NFACTORY : NUSER, 0);
        str_cat(p, " \xB7 K3 LOADS");
        return "SELECT PATCH";
    case LY_SAVE: p = str_cat(sub, "TO USER SLOT B"); fmt_u(p, save_slot + 1u, 2); return pname;
    case LY_SYS: str_cat(sub, "DNB / DUB / DUBSTEP / TECHNO EDITION"); return "DEVICE SETUP";
    case LY_LFO: {
        uint32_t k = vi - PAGE_V0[PG_LFO];
        const int8_t *q = patch + k * LFO_2;
        p = str_cat(t, k ? "MOTION " : "WOBBLE ");
        item_text(k ? I_L2RATE : I_L1RATE, p);
        p = str_cat(sub, L_SHAPE[q[L1_SHP]]);
        p = str_cat(p, " \xB7 ");
        p = str_cat(p, dst_name((uint32_t)q[L1_DST]));
        *p++ = ' ';
        item_text(L1_DEP + k * LFO_2, p);
        return t;
    }
    case LY_STEP:
        p = str_cat(t, "STEP ");
        str_cat(p, L_SPAT[patch[ST_PAT] & 15]);
        p = str_cat(sub, L_SRATE[clampi(patch[ST_RATE], 0, 5)]);
        p = str_cat(p, " \xB7 GATE ");
        item_text(ST_GATE, p); p += str_len(p);
        p = str_cat(p, " \xB7 ");
        str_cat(p, patch[ST_ON] ? dst_name((uint32_t)patch[ST_DST]) : "OFF");
        return t;
    default: break;
    }
    if (page == PG_BASS && vi <= 3u) { uint32_t e = (uint32_t)patch[vi == 2u ? A_ENG : B_ENG] & 7u; str_cat(sub, ENG_SUB[e]); return ENG_TITLE[e]; }
    if (page == PG_BASS && vi == 4u) { str_cat(sub, "LOW END CHARACTER"); return "SUB / NOISE"; }
    if (page == PG_BASS) { str_cat(sub, "TWO ENGINES, ONE BASS"); return patch[X_MODE] == 0 ? "A + B" : patch[X_MODE] == 1 ? "A x B  RING" : "B > A  X-MOD"; }
    if (page == PG_ENV) {
        if (vi == PAGE_V0[PG_ENV] + 2u) str_cat(sub, dst_name((uint32_t)patch[ME_DST]));
        return vi == PAGE_V0[PG_ENV] ? "ADSR CONTROL" : vi == PAGE_V0[PG_ENV] + 1u ? "FILTER ADSR" : "PITCH / MOD";
    }
    if (page == PG_FILT) { str_cat(sub, FT_SUB[patch[F_TYPE]]); return FT_TITLE[patch[F_TYPE]]; }
    if (page == PG_LFO) { str_cat(sub, "4 SLOTS \xB7 SOURCE > DEST"); return "ROUTING"; }
    {
        uint32_t f = vi - PAGE_V0[PG_FX];
        p = str_cat(sub, f == 0u ? L_DRT[patch[DR_TYPE]] : f == 3u ? L_DIV[patch[DL_TIME]] : "");
        if (p != sub) p = str_cat(p, " \xB7 ");
        str_cat(p, f < 5u ? (patch[v->it[0]] ? "ON" : "OFF") : "ALWAYS ON");
        return FX_TITLE[f < 6u ? f : 0u];
    }
}

/* ---------------------------------------------------------------- rows, cells, boxes */
static void draw_row(int32_t y, const char *lab, const char *val, int sel, uint16_t valc);
static void row_g(int32_t x, int32_t y, int32_t w, int32_t h, const char *lab, const char *val, int sel, uint16_t valc, const ks_font_t *f)
{
    int32_t ty = y + (h - f->h) / 2 + 1;
    if (!g_vis(y, h)) return;
    g_box(x, y, w, h, sel ? K_LIMED : K_PANEL, sel ? K_LIME : K_LINE);
    g_text(f, x + 8, ty, lab, K_WHITE);
    g_text_r(f, x + w - 8, ty, val, sel ? K_LIME : valc);
}
static void draw_row(int32_t y, const char *lab, const char *val, int sel, uint16_t valc) { row_g(6, y, 228, 24, lab, val, sel, valc, T_S); }
static void rows_g(const view_t *v, int32_t x, int32_t y0, int32_t w, int32_t h, int32_t step, uint32_t vis, const ks_font_t *f)
{
    uint32_t c = cursor[vidx()], top = 0, i;
    if (v->n > vis) {
        top = c >= 2u ? c - 2u : 0u;
        if (top + vis > v->n) top = v->n - vis;
        if (g_vis(y0, (int32_t)vis * step)) {   /* where we are in the list */
            int32_t hh = (int32_t)vis * step - (step - h), bh = hh * (int32_t)vis / v->n;
            g_rect(x + w + 2, y0, 2, hh, K_TRACK);
            g_rect(x + w + 2, y0 + (hh - bh) * (int32_t)top / (int32_t)(v->n - vis), 2, bh, K_GREY);
        }
    }
    for (i = 0; i < vis && top + i < v->n; i++) {
        uint32_t id = v->it[top + i];
        char b[24];
        item_text(id, b);
        row_g(x, y0 + (int32_t)i * step, w, h, item_name(id), b, top + i == c, id == G_CPU ? K_GREY : K_CYAN, f);
    }
}
static void draw_vbox(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t id, int sel, const char *lab)
{
    char b[24];
    if (!g_vis(y, h)) return;
    item_text(id, b);
    g_box(x, y, w, h, sel ? K_LIMED : K_PANEL, sel ? K_LIME : K_LINE);
    g_text(T_XS, x + 7, y + 6, lab ? lab : item_name(id), sel ? K_LIME : K_GREY);
    if (h >= 50) g_text(T_B, x + 7, y + 18, b, sel ? K_LIME : K_WHITE);
    else g_text(T_M, x + 7, y + 17, b, sel ? K_LIME : K_WHITE);
    g_bar(x + 7, y + h - 10, w - 14, 5, item_frac(id), sel ? K_LIME : K_CYAN);
}
static void draw_cell(const view_t *v, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t id, int sel)   /* STUDIO */
{
    char b[24];
    const char *tag = mod_tag(id);
    int32_t k = knob_of(v, id);
    if (!g_vis(y, h)) return;
    item_text(id, b);
    g_box(x, y, w, h, sel ? K_LIMED : K_PANEL, sel ? K_LIME : K_LINE);
    if (k >= 0) {
        char kn[3] = {'K', (char)('1' + k), 0};
        g_rect(x + 5, y + 5, 5, 5, theme_pal >= 4u ? K_WHITE : KNOB_RGB[k]);
        g_text(T_XS, x + 13, y + 4, kn, K_GREY);
    }
    if (tag) {
        int32_t tw = g_text_w(T_XS, tag) + 6;
        g_box(x + w - tw - 3, y + 3, tw, 10, K_MOD, K_MOD);
        g_text(T_XS, x + w - tw, y + 4, tag, K_INK);
    }
    g_text(T_XS, x + 5, y + 16, item_sname(id), K_GREY);
    g_text(b[0] && g_text_w(T_M, b) > w - 8 ? T_XS : T_M, x + 5, y + 27, b, sel ? K_LIME : K_WHITE);
    g_bar(x + 5, y + h - 9, w - 10, 4, item_frac(id), sel ? K_LIME : K_CYAN);
}

/* the foot: what the knobs do, as many as fit */
static void foot_text(const view_t *v, char *b, int32_t maxw)
{
    char *p;
    uint32_t k, n = 4;
    for (;;) {
        p = b;
        *p = 0;
        for (k = 0; k < n; k++) {
            if (v->k[k] == I_NONE) continue;
            if (p != b) p = str_cat(p, " \xB7 ");
            *p++ = 'K'; *p++ = (char)('1' + k); *p++ = ' ';
            p = str_cat(p, item_sname(v->k[k]));
        }
        if (g_text_w(T_XS, b) < maxw || n <= 1u) break;
        n--;
    }
}
static const char *foot_fixed(uint32_t vi)
{
    switch (VIEWS[vi].lay) {
    case LY_PLAY: return vi == 0u ? "KNOBS = LIVE MACROS" : 0;
    case LY_PRESET: return "K1 SCROLL \xB7 K3 LOAD";
    case LY_SAVE: return "K1 SLOT \xB7 SAVE = WRITE";
    case LY_SYS: return "SELECT ROW \xB7 ALGO VALUE";
    default: return 0;
    }
}
static void view_dots(int32_t xr, int32_t y)    /* the page's views: one dot each, the one shown lit */
{
    uint32_t nv = (uint32_t)(PAGE_V0[page + 1u] - PAGE_V0[page]), i;
    int32_t x = xr - (int32_t)nv * 8;
    for (i = 0; nv > 1u && i < nv; i++) g_rect(x + (int32_t)i * 8, y, 5, 5, i == vcur[page] ? K_LIME : K_LINE);
}

/* ---------------------------------------------------------------- the lists that every theme shares */
static void presets_g(uint32_t bank, int32_t x, int32_t y0, int32_t w, int32_t h, int32_t step, uint32_t vis, const ks_font_t *f)
{
    uint32_t c = pre_cur[bank], n = bank ? NUSER : NFACTORY, top, i;
    top = c >= 2u ? c - 2u : 0u;
    if (top + vis > n) top = n - vis;
    for (i = 0; i < vis; i++) {
        uint32_t k = top + i;
        int32_t y = y0 + (int32_t)i * step;
        char b[24], *p;
        const char *nm = bank ? (up_valid(k) ? ustore[k].name : "- EMPTY -") : FACTORY[k].name;
        if (!g_vis(y, h)) continue;
        p = fmt_u(b, k + 1u, 2);
        *p++ = fav_is(bank, k) ? '*' : ' '; *p++ = ' ';
        str_cat(p, nm);
        g_box(x, y, w, h, k == c ? K_LIMED : K_PANEL, k == c ? K_LIME : K_LINE);
        g_text(f, x + 8, y + (h >= 30 ? 4 : (h - f->h) / 2 + 1), b, k == c ? K_LIME : K_WHITE);
        if (!bank || up_valid(k)) {             /* its music and its engines, small */
            int8_t pp[NP_MAX];
            char nb[14], e[32];
            if (bank) memcpy(pp, ustore[k].p, NP_MAX); else preset_factory(k, pp, nb);
            eng_str(e, pp);
            if (h >= 30) {
                g_text(T_XS, x + 8, y + h - 11, e, K_CYAN);
                g_text_r(T_XS, x + w - 16, y + h - 11, L_STYLE[clampi(pp[P_STYLE], 0, NSTYLE - 1)], K_MOD);
            } else {                            /* (only what fits beside the name: wide fonts) */
                const char *st = L_STYLE[clampi(pp[P_STYLE], 0, NSTYLE - 1)];
                int32_t room = w - 30 - g_text_w(f, b);
                if (g_text_w(T_XS, st) < room) g_text_r(T_XS, x + w - 14, y + h / 2 - 8, st, k == c ? K_LIME : K_GREY);
                if (g_text_w(T_XS, e) < room) g_text_r(T_XS, x + w - 14, y + h / 2 + 1, e, K_CYAN);
            }
        }
        if (set.bank == bank && set.idx == k) { /* the one playing */
            int32_t j;
            for (j = 0; j < 4; j++) g_rect(x + w - 10 + j, y + h / 2 - 4 + j, 1, 8 - 2 * j, k == c ? K_LIME : K_CYAN);
        }
    }
}
static void save_g(int32_t x, int32_t y0, int32_t w, int32_t ty)   /* the name being written, its slot */
{
    char sub[32], b[8], *p;
    int32_t cx = x, i;
    for (i = 0; i < (int32_t)save_pos; i++) { char ch[2] = {pname[i], 0}; cx += g_text_w(T_L, ch); }
    if (g_vis(ty + T_L->h, 2)) { char ch[2] = {pname[save_pos] ? pname[save_pos] : ' ', 0}; g_rect(cx, ty + T_L->h, g_text_w(T_L, ch), 2, K_LIME); }
    p = str_cat(b, "B");
    fmt_u(p, save_slot + 1u, 2);
    row_g(x - 2, y0, w, 24, "SLOT", b, 1, K_CYAN, T_S);
    str_cat(sub, up_valid(save_slot) ? ustore[save_slot].name : "- EMPTY -");
    row_g(x - 2, y0 + 29, w, 24, "NOW IN SLOT", sub, 0, K_GREY, T_S);
    row_g(x - 2, y0 + 58, w, 24, "STYLE", L_STYLE[clampi(patch[P_STYLE], 0, NSTYLE - 1)], 0, K_CYAN, T_S);
    if (g_vis(y0 + 90, 26)) {
        g_text(T_XS, x, y0 + 92, "K2 POSITION \xB7 K3 LETTER \xB7 K4 STYLE", K_GREY);
        g_text(T_XS, x, y0 + 104, "PRESS SAVE AGAIN TO WRITE", K_LIME);
    }
}

/* ---------------------------------------------------------------- CLASSIC */
static void th_classic(const view_t *v, uint32_t vi)
{
    char t[32], sub[48], n[8], fb[64], *p;
    const char *ti = view_title(vi, t, sub), *ff = foot_fixed(vi);
    uint32_t vz = VZ_OF[vi], c = cursor[vi], i;
    if (g_vis(0, 36)) {
        g_text(T_M, 8, 5, "K-WA SUB", K_LIME);
        p = fmt_u(n, (uint32_t)page + 1u, 2);
        if (page == PG_PLAY) str_cat(p, "/08");
        g_text_r(T_XS, 232, 7, n, K_GREY);
        view_dots(232 - g_text_w(T_XS, n) - 6, 8);
        g_rect(6, 19, 228, 1, K_LINE);
        g_text(T_S, 8, 24, v->cat, K_LIME);
    }
    if (v->lay == LY_PLAY) {                    /* the patch, a scope, four macros */
        g_text(T_L, 8, 37, ti, K_WHITE);
        g_text(T_XS, 8, 61, sub, K_GREY);
        p = t;
        if (hold) p = str_cat(p, "HOLD \xB7 ");
        p = fmt_u(p, ks_bpm10 / 10u, 0);
        str_cat(p, " BPM");
        g_text_r(T_XS, 232, 26, t, hold ? K_LIME : K_CYAN);
        draw_visual(VZ_SCOPE, 4, 72, 232, 38, 0);
        for (i = 0; i < 4u; i++)
            draw_vbox(i & 1u ? 123 : 6, 116 + (int32_t)(i >> 1) * 52, 111, 47, v->it[i], i == c,
                      v->it[i] == I_L1RATE ? "WOB RATE" : v->it[i] == L1_DEP ? "WOB DEPTH" : 0);
    } else {
        g_text(T_L, 8, 37, ti, K_WHITE);
        g_text(T_XS, 8, 61, sub, v->lay == LY_LIST && page == PG_FX ? K_ORANGE : K_GREY);
        if (v->lay == LY_PRESET) presets_g(vi - PAGE_V0[PG_PRE], 6, 74, 228, 24, 29, 5, T_S);
        else if (v->lay == LY_SAVE) save_g(8, 74, 228, 37);
        else if (v->lay == LY_BOX) {
            draw_visual(vz, 6, 72, 228, 46, 1);
            for (i = 0; i < 4u; i++) draw_vbox(i & 1u ? 123 : 6, 122 + (int32_t)(i >> 1) * 50, 111, 46, v->it[i], i == c, 0);
        } else if (vz) {
            draw_visual(vz, 6, 72, 228, 38, 1);
            rows_g(v, 6, 114, 228, 23, 27, 4, T_S);
        } else
            rows_g(v, 6, 74, 228, 24, 29, 5, T_S);
    }
    if (g_vis(222, 12)) {
        if (ff) str_cat(fb, ff); else foot_text(v, fb, 180);
        g_text(T_XS, 8, 226, fb, K_GREY);
        g_text_r(T_XS, 232, 226, v->tag, K_CYAN);
    }
}

/* ---------------------------------------------------------------- STUDIO */
static void th_studio(const view_t *v, uint32_t vi)
{
    char t[32], sub[48], s[40], fb[64], *p;
    const char *ti = view_title(vi, t, sub), *ff = foot_fixed(vi);
    uint32_t vz = VZ_OF[vi], c = cursor[vi], i;
    if (g_vis(0, 28)) {                         /* status: the patch, its slot, the tempo, the load; the eight pages */
        g_rect(0, 0, 240, 15, K_PANEL);
        g_text(T_S, 5, 3, pname, pdirty ? K_ORANGE : K_WHITE);
        p = str_cat(s, set.bank ? "B" : "A");
        p = fmt_u(p, (uint32_t)set.idx + 1u, 2);
        p = str_cat(p, "  ");
        p = fmt_u(p, ks_bpm10 / 10u, 0);
        if (hold) p = str_cat(p, "  HOLD");
        p = str_cat(p, "  C0 ");
        p = fmt_u(p, ks_load[0], 0);
        str_cat(p, "%");
        g_text_r(T_XS, 235, 4, s, K_GREY);
        for (i = 0; i < NPAGE; i++) {
            int32_t x = 1 + (int32_t)i * 30;
            if (i == page) g_box(x, 17, 28, 11, K_LIME, K_LIME);
            else g_box(x, 17, 28, 11, K_BG, K_LINE);
            g_text_c(T_XS, x + 14, 19, PAGE_SHORT[i], i == page ? K_INK : K_GREY);
        }
    }
    if (g_vis(30, 32)) {
        g_text(T_XS, 5, 31, v->cat, K_LIME);
        view_dots(235, 32);
        g_text(T_B, 5, 41, ti, K_WHITE);
        g_text(T_XS, 5, 59, sub, K_MOD);
    }
    if (v->lay == LY_PLAY) {                    /* scope, then the four macros as long bars in the knobs' colours */
        draw_visual(VZ_SCOPE, 3, 70, 234, 44, 1);
        for (i = 0; i < 4u; i++) {
            uint32_t id = v->it[i];
            int32_t y = 120 + (int32_t)i * 25;
            char b[24];
            if (!g_vis(y, 24)) continue;
            item_text(id, b);
            g_rect(5, y + 5, 6, 6, theme_pal >= 4u ? K_WHITE : KNOB_RGB[i]);
            g_text(T_S, 16, y + 3, id == I_L1RATE ? "WOB RATE" : id == L1_DEP ? "WOB DEPTH" : item_name(id), i == c ? K_LIME : K_GREY);
            g_text_r(T_M, 235, y + 2, b, K_WHITE);
            g_bar(16, y + 17, 219, 4, item_frac(id), theme_pal >= 4u ? K_WAVE : KNOB_RGB[i]);
        }
    } else if (v->lay == LY_PRESET) presets_g(vi - PAGE_V0[PG_PRE], 3, 70, 234, 28, 31, 5, T_S);
    else if (v->lay == LY_SAVE) save_g(6, 98, 228, 70);
    else if (v->lay == LY_SYS || !v->n || vi == PAGE_V0[PG_LFO] + 3u) rows_g(v, 3, 70, 230, 22, 26, 5, T_S);
    else {                                      /* the items in cells of four, a page of eight round the row */
        uint32_t per = v->n <= 4u ? 4u : 8u, first = (c / per) * per;
        for (i = 0; i < per && first + i < v->n; i++)
            draw_cell(v, 3 + (int32_t)(i & 3u) * 59, 70 + (int32_t)(i >> 2) * 60, 56, 56, v->it[first + i], first + i == c);
        if (v->n > 8u && g_vis(186, 4)) {       /* which eight */
            uint32_t pg, np = (v->n + 7u) / 8u;
            for (pg = 0; pg < np; pg++) g_rect(110 + (int32_t)pg * 8, 186, 5, 3, pg == c / 8u ? K_LIME : K_LINE);
        }
        if (per == 4u) draw_visual(vz, 3, 132, 234, 86, 1);
        else draw_visual(vz, 3, 192, 234, 28, 1);
    }
    if (g_vis(222, 12)) {
        if (ff) str_cat(fb, ff); else foot_text(v, fb, 200);
        g_text(T_XS, 5, 226, fb, K_GREY);
        g_text_r(T_XS, 235, 226, v->tag, K_CYAN);
    }
}

/* ---------------------------------------------------------------- PURE */
static void th_pure(const view_t *v, uint32_t vi)
{
    char t[32], sub[48], s[40], *p;
    const char *ti = view_title(vi, t, sub);
    uint32_t vz = VZ_OF[vi], c = cursor[vi], i, nv = (uint32_t)(PAGE_V0[page + 1u] - PAGE_V0[page]);
    if (g_vis(0, 18)) {                         /* the section on a lime tab with a cut corner */
        int32_t w = g_text_w(T_S, v->cat) + 18, y;
        for (y = 0; y < 17; y++) if (g_vis(y, 1)) g_rect(0, y, w + (16 - y) / 2 + 4, 1, K_LIME);
        g_text(T_S, 7, 4, v->cat, K_INK);
        p = fmt_u(s, (uint32_t)page + 1u, 2);
        if (nv > 1u) { p = str_cat(p, " \xB7 "); p = fmt_u(p, vcur[page] + 1u, 0); p = str_cat(p, "/"); fmt_u(p, nv, 0); }
        g_text_r(T_XS, 234, 5, s, K_GREY);
    }
    if (v->lay == LY_PRESET || v->lay == LY_SAVE || v->lay == LY_SYS || !vz) {   /* lists: rows under the title */
        g_text(T_B, 8, 22, ti, K_WHITE);
        g_text(T_XS, 8, 40, sub, K_GREY);
        if (v->lay == LY_PRESET) presets_g(vi - PAGE_V0[PG_PRE], 6, 52, 228, 28, 32, 5, T_S);
        else if (v->lay == LY_SAVE) save_g(8, 70, 228, 22);
        else rows_g(v, 6, 52, 226, 24, 29, 6, T_S);
        if (g_vis(222, 12)) { char fb[64]; const char *ff = foot_fixed(vi); if (ff) str_cat(fb, ff); else foot_text(v, fb, 220); g_text(T_XS, 8, 228, fb, K_GREY); }
        return;
    }
    if (g_vis(20, 28)) {                        /* the title between chevrons */
        g_text(T_M, 6, 23, "<", K_GREY);
        g_text_r(T_M, 234, 23, ">", K_GREY);
        g_text_c(T_B, 120, 21, ti, K_WHITE);
        g_text_c(T_XS, 120, 39, sub, K_GREY);
    }
    draw_visual(vz, 6, 49, 228, 76, 1);
    for (i = 0; i < 4u; i++) {                  /* the four knobs as rings */
        uint32_t id = v->lay == LY_PLAY ? v->it[i] : v->k[i];
        int32_t cx = 34 + (int32_t)i * 57;
        char b[24];
        if (id == I_NONE || !g_vis(128, 76)) continue;
        item_text(id, b);
        g_ring(cx, 148, 17, item_frac(id), (v->lay == LY_PLAY ? i == c : v->it[c] == id) ? K_LIME : K_WAVE);
        g_text_c(T_XS, cx, 170, id == I_L1RATE || id == I_L2RATE ? "RATE" : item_sname(id), K_GREY);
        g_text_c(g_text_w(T_S, b) > 54 ? T_XS : T_S, cx, 180, b, K_WHITE);
    }
    if (g_vis(196, 44)) {                       /* the row being edited (SELECT, ALGO), then what moves the page */
        uint32_t id = v->it[c];
        char b[24];
        g_box(4, 196, 232, 40, K_PANEL, K_LINE);
        item_text(id, b);
        {
            const char *lb = v->lay == LY_PLAY ? "MACRO" : "EDIT";
            g_text(T_XS, 10, 201, lb, K_LIME);
            g_text(T_S, 16 + g_text_w(T_XS, lb), 200, id == I_L1RATE ? "WOB RATE" : item_name(id), K_WHITE);
        }
        g_text_r(T_S, 230, 200, b, K_WAVE);
        p = s;
        if (v->lay == LY_PLAY) { p = str_cat(p, hold ? "HOLD \xB7 " : ""); p = fmt_u(p, ks_bpm10 / 10u, 0); str_cat(p, " BPM \xB7 PRESETS: NEXT"); }
        else {
            const char *tg = mod_tag(id);
            p = str_cat(p, "MOD ");
            str_cat(p, tg ? tg : "\xB7 NONE ON THIS ROW");
        }
        g_text(T_XS, 10, 220, s, K_GREY);
    }
}

/* ---------------------------------------------------------------- RACK */
static void th_rack(const view_t *v, uint32_t vi)
{
    char t[32], sub[48], fb[64];
    const char *ti = view_title(vi, t, sub), *ff = foot_fixed(vi);
    uint32_t vz = VZ_OF[vi], i;
    if (g_vis(0, 240)) {                        /* the rail: the eight pages */
        if (g_vis(g_y0, 1)) g_rect(0, g_y0, 31, (int32_t)BAND_H, K_PANEL);
        for (i = 0; i < NPAGE; i++) {
            int32_t y = 3 + (int32_t)i * 29;
            char nb[3];
            if (!g_vis(y, 27)) continue;
            if (i == page) g_box(2, y, 27, 26, K_LIME, K_LIME);
            fmt_u(nb, i + 1u, 0);
            g_text_c(T_XS, 15, y + 3, nb, i == page ? K_INK : K_GREY);
            g_text_c(T_XS, 15, y + 14, PAGE_SHORT[i], i == page ? K_INK : K_WHITE);
        }
    }
    if (g_vis(0, 50)) {
        g_text(T_S, 37, 4, v->cat, K_LIME);
        view_dots(235, 6);
        g_text(T_L, 37, 17, ti, K_WHITE);
        g_text(T_XS, 37, 40, sub, K_GREY);
    }
    if (v->lay == LY_PLAY) {
        uint32_t c = cursor[vi];
        draw_visual(VZ_SCOPE, 35, 51, 201, 46, 1);
        for (i = 0; i < 4u; i++) {
            uint32_t id = v->it[i];
            char b[24];
            item_text(id, b);
            row_g(35, 101 + (int32_t)i * 29, 199, 26, id == I_L1RATE ? "WOB RATE" : id == L1_DEP ? "WOB DEPTH" : item_name(id), b, i == c, K_CYAN, T_M);
        }
    } else if (v->lay == LY_PRESET) presets_g(vi - PAGE_V0[PG_PRE], 35, 51, 199, 31, 34, 5, T_M);
    else if (v->lay == LY_SAVE) save_g(37, 70, 199, 17);
    else if (vz) {
        draw_visual(vz, 35, 51, 201, 36, 1);
        rows_g(v, 35, 91, 197, 24, 26, 5, T_M);
    } else
        rows_g(v, 35, 51, 197, 24, 26, 6, T_M);
    if (g_vis(222, 12)) {
        if (ff) str_cat(fb, ff); else foot_text(v, fb, 190);
        g_text(T_XS, 37, 228, fb, K_GREY);
    }
}

static void draw_all(void)
{
    const view_t *v = view();
    uint32_t vi = vidx();
    g_rect(0, g_y0, 240, (int32_t)BAND_H, K_BG);
    switch (theme) {
    case TH_STUDIO: th_studio(v, vi); break;
    case TH_PURE: th_pure(v, vi); break;
    case TH_RACK: th_rack(v, vi); break;
    default: th_classic(v, vi); break;
    }
    if (dpop_ms && g_vis(22, 200)) {            /* the destination list: the ones before and after, the group of each */
        static const char *const GR[] = {"", "FILTER", "FILTER", "PITCH", "OSC A", "OSC A", "OSC B", "OSC B", "OSC A", "OSC B",
                                         "MIX", "FILTER", "AMP", "MIX", "MIX", "MOD", "OSC A", "OSC B", "OSC A", "OSC B", "MIX",
                                         "FILTER", "MOD", "MOD", "MOD", "STEP", "SAT", "SAT", "CRUSH", "CRUSH", "CRUSH", "CHORUS",
                                         "CHORUS", "CHORUS", "DELAY", "DELAY", "DELAY", "REVERB", "REVERB", "REVERB", "MASTER",
                                         "MASTER", "MASTER"};
        uint8_t ls[ND];
        int32_t n = 0, at = 0, i, cur = patch[dpop_id], y;
        char hd[24], pos[12], *p;
        for (i = 0; i < ND; i++)
            if (dst_on(DST_ORDER[i]) || DST_ORDER[i] == cur) { if (DST_ORDER[i] == cur) at = n; ls[n++] = DST_ORDER[i]; }
        g_box(18, 22, 204, 198, K_PANEL, K_LIME);
        p = str_cat(hd, dpop_id == ME_DST ? "MOD ENV" : dpop_id == ST_DST ? "STEP" : dpop_id == L2_DST ? "LFO 2" :
                        dpop_id == L1_DST ? "LFO 1" : "MATRIX");
        str_cat(p, " \xB7 DEST");
        g_text(T_XS, 26, 27, hd, K_LIME);
        p = fmt_u(pos, (uint32_t)at + 1u, 0);
        p = str_cat(p, "/");
        fmt_u(p, (uint32_t)n, 0);
        g_text_r(T_XS, 214, 27, pos, K_GREY);
        for (i = -5; i <= 5; i++) {
            int32_t k = at + i;
            y = 112 + i * 16;
            if (k < 0 || k >= n) continue;
            if (!i) g_box(22, y - 2, 196, 16, K_LIME, K_LIME);
            g_text(T_S, 30, y + 1, dst_name(ls[k]), !i ? K_INK : (i == -1 || i == 1) ? K_WHITE : K_GREY);
            g_text_r(T_XS, 212, y + 2, GR[ls[k] < sizeof GR / sizeof GR[0] ? ls[k] : 0], !i ? K_INK : K_MOD);
        }
        if (at > 5) g_text_c(T_XS, 120, 38, "\x5E", K_GREY);
        if (n - at > 6) g_text_c(T_XS, 120, 206, "v", K_GREY);
    }
    if (pend_on && g_vis(126, 90)) {            /* the question */
        char q[32], e[32], *p;
        int8_t pp[NP_MAX];
        char nb[14];
        g_box(14, 126, 212, 88, K_PANEL, K_ORANGE);
        g_text_c(T_XS, 120, 134, "SOUND CHANGED \xB7 NOT SAVED", K_ORANGE);
        p = str_cat(q, "LOAD ");
        if (pend_bank) { p = str_cat(p, "B"); p = fmt_u(p, pend_k + 1u, 2); memcpy(pp, ustore[pend_k].p, NP_MAX); }
        else { p = fmt_u(p, pend_k + 1u, 2); preset_factory(pend_k, pp, nb); }
        p = str_cat(p, " ");
        str_cat(p, pend_bank ? ustore[pend_k].name : FACTORY[pend_k].name);
        g_text_c(T_S, 120, 150, q, K_WHITE);
        p = str_cat(e, L_STYLE[clampi(pp[P_STYLE], 0, NSTYLE - 1)]);
        p = str_cat(p, " \xB7 ");
        eng_str(p, pp);
        g_text_c(T_XS, 120, 168, e, K_CYAN);
        g_text_c(T_XS, 120, 188, "SEL = LOAD \xB7 OTHER BUTTON = KEEP", K_LIME);
        g_text_c(T_XS, 120, 200, "PRESETS: ANOTHER ONE", K_GREY);
    }
    if (vpop_ms && !dpop_ms && !pend_on && g_vis(72, 66)) {   /* the value turned, big: before -> now */
        char now[24], t[40];
        str_cat(str_cat(t, vpop_pre), item_name(vpop_id));
        item_text(vpop_id, now);
        g_box(20, 72, 200, 66, K_PANEL, K_LIME);
        g_text_c(T_XS, 120, 77, t, K_GREY);
        g_text_c(g_text_w(T_L, now) > 190 ? T_B : T_L, 120, 92, now, K_WHITE);
        if (!str_eq(now, vpop_before)) { str_cat(str_cat(t, "WAS "), vpop_before); g_text_c(T_XS, 120, 124, t, K_MOD); }
    }
    if (cmp_on && g_vis(0, 18)) {               /* A/B: the saved sound is playing */
        g_rect(0, 0, 240, 18, K_ORANGE);
        g_text_c(T_XS, 120, 4, "A/B \xB7 SAVED SOUND \xB7 RELEASE SEL", K_INK);
    }
    if (hb.b != 0xFF && !hb.used && !hb.fired && fm1_ms - hb.at > 180u && g_vis(178, 42)) {   /* a button held: what comes */
        uint32_t el = fm1_ms - hb.at, fr = hb.next ? (el >= hb.next ? 256u : el * 256u / hb.next) : 0u;
        g_box(14, 180, 212, 38, K_PANEL, hb.next ? K_LIME : K_MOD);
        g_text_c(T_XS, 120, 186, hold_label(hb.b), hb.next ? K_WHITE : K_MOD);
        if (hb.next) g_bar(24, 202, 192, 7, fr, K_LIME);
        else g_text_c(T_XS, 120, 202, "WAITING FOR A KNOB...", K_GREY);
    }
    if (splash_until && (int32_t)(splash_until - fm1_ms) > 0) {   /* the start: the last patch, the cores */
        char b[40], *p;
        g_rect(0, g_y0, 240, (int32_t)BAND_H, K_BG);
        g_text_c(T_L, 120, 52, "K-WA SUB", K_LIME);
        g_text_c(T_XS, 120, 80, "BASS SYNTH FOR THE FM-1 \xB7 V" APP_VER, K_GREY);
        g_text_c(T_B, 120, 112, pname, K_WHITE);
        p = str_cat(b, L_STYLE[clampi(patch[P_STYLE], 0, NSTYLE - 1)]);
        p = str_cat(p, " \xB7 ");
        eng_str(p, patch);
        g_text_c(T_XS, 120, 134, b, K_CYAN);
        g_text_c(T_S, 120, 170, dual_on ? "TWO CORES \xB7 VOICES ON CORE 1" : KS_DUAL && set.cores ? "ONE CORE (CORE 1 OFF)" : "ONE CORE", dual_on ? K_LIME : K_ORANGE);
        g_text_c(T_XS, 120, 200, "ANY BUTTON TO START", K_GREY);
    }
    if (toast_ms && g_vis(196, 22)) {
        g_box(30, 196, 180, 22, K_PANEL, K_LIME);
        g_text_c(T_S, 120, 202, toast_txt, K_WHITE);
    }
}
