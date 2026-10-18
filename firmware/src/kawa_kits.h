/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the drum kits (the KIT engine, kawa_drums.c): 8 kits of 16 drums on the 16 white keys, their names
 * and the codes the tracker shows. Included before kawa_data.c (its KIT sound lists the kits).
 * Every kit keeps key 1 a kick, 2 a second kick (the magic beat's hard one), 3 a snare, 5 a closed hat, 6 an
 * open hat (PERCU: the low conga, the high one, the slap, the shaker, the tambourine), so a beat survives a
 * change of kit; the other keys are the kit's own */
#define NDRUM 16u
#define NKIT 8u
static const char *const L_KITS[NKIT * 3u] = {   /* (K_LIST: French, English, Spanish) */
    "ACOUST.", "HIP HOP", "TECHNO", "DUB", "LO-FI", "GAMEBOY", "SNES", "PERCUS",
    "ACOUST.", "HIP HOP", "TECHNO", "DUB", "LO-FI", "GAMEBOY", "SNES", "PERCS",
    "ACUST.", "HIP HOP", "TECHNO", "DUB", "LO-FI", "GAMEBOY", "SNES", "PERCUS"};
static const char KIT_CODE3[NKIT][NDRUM][4] = {   /* what the tracker shows, wide (3) and narrow (2) */
    {"KIK", "ROK", "SNR", "STK", "CHH", "OHH", "LTM", "HTM", "CRS", "RID", "BEL", "FTM", "PHH", "BRS", "RMS", "FLM"},
    {"KIK", "808", "SNR", "RIM", "CHH", "OHH", "LTM", "HTM", "CRS", "SNP", "SHK", "TAM", "VNL", "SCR", "KCK", "CLP"},
    {"KIK", "HRD", "SNR", "CLP", "CHH", "OHH", "LTM", "HTM", "RID", "CRS", "RIM", "RUM", "COW", "PRC", "CLV", "ZAP"},
    {"KIK", "STP", "SNR", "RIM", "CHH", "OHH", "LTM", "HTM", "SHK", "SYN", "SIR", "SPL", "BLK", "FUN", "REP", "LAS"},
    {"KIK", "THP", "SNR", "RIM", "CHH", "OHH", "TOM", "TM2", "SNP", "SHK", "TNG", "VNL", "BRS", "CLP", "KNK", "DWN"},
    {"KIK", "PKK", "SNR", "SN7", "CHH", "OHH", "TOM", "TM2", "CRS", "BZZ", "BLP", "ZAP", "EXP", "JMP", "CON", "CLP"},
    {"KIK", "PWR", "SNR", "ORS", "CHH", "OHH", "TIM", "TIH", "CRS", "ORC", "TOM", "TM2", "BEL", "TAI", "CLP", "STL"},
    {"CGL", "CGH", "SLP", "TIM", "SHK", "TAM", "BGL", "BGH", "CLV", "COW", "AGL", "AGH", "GUI", "TRI", "WBK", "CAB"}};
static const char KIT_CODE2[NKIT][NDRUM][3] = {
    {"KI", "RK", "SN", "SS", "HH", "OH", "LT", "HT", "CR", "RD", "BL", "FT", "PH", "BR", "RS", "FL"},
    {"KI", "80", "SN", "RM", "HH", "OH", "LT", "HT", "CR", "SP", "SK", "TB", "VN", "SC", "KK", "CP"},
    {"KI", "HD", "SN", "CP", "HH", "OH", "LT", "HT", "RD", "CR", "RM", "RU", "CB", "PC", "CL", "ZP"},
    {"KI", "ST", "SN", "RM", "HH", "OH", "LT", "HT", "SK", "SY", "SI", "SP", "WB", "FU", "RP", "LZ"},
    {"KI", "TH", "SN", "RM", "HH", "OH", "TM", "T2", "SP", "SK", "TG", "VN", "BR", "CP", "KN", "DW"},
    {"KI", "PK", "SN", "S7", "HH", "OH", "TM", "T2", "CR", "BZ", "BP", "ZP", "EX", "JP", "CN", "CP"},
    {"KI", "PW", "SN", "OS", "HH", "OH", "TI", "TH", "CR", "OR", "TM", "T2", "BL", "TK", "CP", "SD"},
    {"CL", "CH", "SL", "TI", "SK", "TB", "BL", "BH", "CV", "CB", "AL", "AH", "GU", "TR", "WB", "CA"}};

/* a drum (kawa_drums.c). Times are DECAY_K indices, 7.6 ln(ms): 8 = 3 ms, 16 = 8, 24 = 24, 30 = 52, 34 = 88,
 * 38 = 148, 42 = 251, 46 = 424, 50 = 718, 54 = 1215. Levels: 128 = full scale */
typedef struct {
    uint8_t wave, note;                         /* tone A: its wave (W_*), its pitch (a MIDI note) */
    int8_t drop;                                /* semitones above the note at the hit (below if < 0) */
    uint8_t ptau, atau, tone;                   /* the drop's time (DR_STEP: when, 5.8 ms a step), the decay, level */
    uint8_t w2;                                 /* tone B: its wave | B_RING or B_FM */
    uint16_t r2;                                /* its pitch over A's, Q8 (256: the same) */
    uint8_t l2, btau;                           /* its level (FM: the index; ring: the depth), decay (0: A's) */
    uint8_t src, clk, noise, ntau;              /* the noise: N_* (| a metal tuning << 4), its clock (a MIDI note,
                                                 * 0: every frame; the metal's base; crackle: clicks / 256 frames),
                                                 * the body's level and decay */
    uint8_t filt, fnote;                        /* the filter: F_* | resonance 0..15 << 2, its cutoff (a MIDI note) */
    uint8_t click, ctau, hits, hgap;            /* the spike (through the filter), its decay, re-strikes, their gap
                                                 * (chunks of 16 frames, 0.36 ms) */
    uint8_t rise, wob, wrate;                   /* an attack (a time), a wobble (1/16 semitone) at a rate (1/4 Hz) */
    uint8_t srate, bits, drive, vol, flags;     /* the hold (SR_*), bits taken, drive, gain (128: x1), DR_* */
} drumdef_t;
enum { W_SINE, W_TRI, W_SQUARE, W_PULSE, W_PULSE12, W_SAW, W_GB4 };
enum { B_RING = 0x10, B_FM = 0x20 };
enum { N_WHITE, N_LFSR15, N_LFSR7, N_METAL, N_CRACKLE };
enum { F_OFF, F_LP, F_BP, F_HP };
enum { SR_OFF, SR_32K, SR_26K, SR_22K, SR_16K, SR_11K, SR_8K, SR_5K };
enum { DR_TFILT = 1, DR_FSWEEP = 2, DR_HTONE = 4, DR_TAIL = 8, DR_HAT = 16, DR_WRND = 32, DR_GB = 64, DR_STEP = 128 };
/* DR_TFILT the tones through the filter too; DR_FSWEEP the cutoff follows the pitch envelope; DR_HTONE a re-strike
 * strikes it all again (a flam); DR_TAIL the body waits for the last re-strike (a clap's room); DR_HAT the hats
 * choke each other; DR_WRND the wobble starts anywhere; DR_GB the Game Boy's stepped volume and pitch; DR_STEP the
 * pitch steps (drop, then the note) instead of falling */

/* a row: tone A, tone B, the noise, the filter, the spike, the modulations, the output */
#define DT(w, n, d, pt, at, l) w, n, d, pt, at, l
#define DB(w, r, l, bt) w, r, l, bt
#define DN(s, c, l, nt) s, c, l, nt
#define DF(m, q, fn) (m) | (q) << 2, fn
#define DK(l, ct, h, hg) l, ct, h, hg
#define DM(r, wb, wr) r, wb, wr
#define DO(sr, b, dr, vo, fl) sr, b, dr, vo, fl
#define T0 DT(W_SINE, 60, 0, 0, 0, 0)
#define B0 DB(W_SINE, 256, 0, 0)
#define N0 DN(N_WHITE, 0, 0, 0)
#define F0 DF(F_OFF, 0, 0)
#define K0 DK(0, 0, 0, 0)
#define M0 DM(0, 0, 0)
#define MTL(set) (N_METAL | (set) << 4)
static const drumdef_t KITS[NKIT][NDRUM] = {
    {   /* ACOUSTIC: woody kicks with a beater, snares with wires, natural toms, a crash, a ride, brushes */
        {DT(W_SINE, 33, 7, 18, 34, 110), DB(W_SINE, 589, 40, 26), DN(N_WHITE, 0, 14, 22), DF(F_LP, 1, 98),
         DK(70, 8, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                  /* KICK, a felt beater */
        {DT(W_SINE, 34, 12, 16, 30, 120), DB(W_SINE, 407, 40, 22), N0, DF(F_BP, 2, 100),
         DK(110, 8, 0, 0), M0, DO(SR_OFF, 0, 2, 128, 0)},                                 /* ROCK KICK, plastic */
        {DT(W_SINE, 54, 2, 20, 26, 70), DB(W_SINE, 410, 45, 0), DN(N_WHITE, 0, 120, 34), DF(F_BP, 1, 108),
         DK(110, 12, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                /* SNARE: shells, wires */
        {DT(W_SINE, 70, 0, 0, 16, 110), DB(W_SINE, 937, 60, 10), N0, DF(F_BP, 6, 91),
         DK(110, 6, 0, 0), M0, DO(SR_OFF, 0, 3, 128, 0)},                                 /* SIDE STICK */
        {DT(W_SQUARE, 103, 0, 0, 18, 30), DB(W_SQUARE | B_RING, 361, 128, 0), DN(N_WHITE, 0, 130, 20), DF(F_HP, 1, 113),
         DK(60, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, DR_HAT | DR_TFILT)},                 /* CLOSED HAT */
        {DT(W_SQUARE, 103, 0, 0, 36, 30), DB(W_SQUARE | B_RING, 361, 128, 0), DN(N_WHITE, 0, 120, 39), DF(F_HP, 1, 112),
         DK(50, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, DR_HAT | DR_TFILT)},                 /* OPEN HAT */
        {DT(W_SINE, 43, 3, 34, 40, 110), DB(W_SINE, 407, 35, 32), DN(N_WHITE, 0, 16, 26), DF(F_LP, 1, 91),
         DK(50, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                 /* LOW TOM */
        {DT(W_SINE, 50, 3, 32, 37, 110), DB(W_SINE, 407, 35, 30), DN(N_WHITE, 0, 16, 24), DF(F_LP, 1, 94),
         DK(50, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                 /* HIGH TOM */
        {DT(W_SQUARE, 94, 0, 0, 46, 40), DB(W_SQUARE | B_RING, 443, 128, 0), DN(N_WHITE, 0, 120, 48), DF(F_HP, 0, 105),
         DK(90, 14, 0, 0), M0, DO(SR_OFF, 0, 0, 128, DR_TFILT)},                          /* CRASH */
        {DT(W_SINE, 93, 0, 0, 48, 60), DB(W_SINE | B_FM, 707, 50, 30), DN(N_WHITE, 0, 50, 46), DF(F_BP, 2, 114),
         DK(60, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                 /* RIDE: a ping, a wash */
        {DT(W_SINE, 89, 0, 0, 48, 90), DB(W_SINE | B_FM, 361, 80, 40), N0, DF(F_HP, 1, 110),
         DK(50, 8, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                  /* RIDE BELL */
        {DT(W_SINE, 38, 4, 36, 43, 115), DB(W_SINE, 407, 35, 34), DN(N_WHITE, 0, 16, 26), DF(F_LP, 1, 88),
         DK(50, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                 /* FLOOR TOM */
        {DT(W_SQUARE, 98, 0, 0, 14, 25), DB(W_SQUARE | B_RING, 361, 128, 0), DN(N_WHITE, 0, 120, 17), DF(F_BP, 3, 106),
         K0, DM(8, 0, 0), DO(SR_OFF, 0, 0, 170, DR_HAT | DR_TFILT)},                      /* PEDAL HAT */
        {T0, B0, DN(N_WHITE, 0, 150, 37), DF(F_BP, 1, 102), K0, DM(30, 0, 0), DO(SR_OFF, 0, 0, 128, 0)},   /* BRUSH */
        {DT(W_SINE, 56, 2, 18, 28, 80), DB(W_SINE, 691, 70, 30), DN(N_WHITE, 0, 90, 32), DF(F_BP, 2, 104),
         DK(150, 10, 0, 0), M0, DO(SR_OFF, 0, 4, 128, 0)},                                /* RIMSHOT */
        {DT(W_SINE, 63, 2, 16, 22, 60), DB(W_SINE, 410, 40, 0), DN(N_WHITE, 0, 120, 29), DF(F_BP, 1, 115),
         DK(110, 10, 1, 60), M0, DO(SR_OFF, 0, 1, 128, DR_HTONE)},                        /* FLAM: a piccolo, twice */
    },
    {   /* HIP HOP: boom bap through an SP-1200 (26 kHz, 12 bits): a deep kick, an 808, a cracking snare, dusty
         * hats, a snap, a tambourine, vinyl, a scratch */
        {DT(W_SINE, 31, 12, 22, 42, 115), DB(W_SINE, 512, 25, 30), N0, DF(F_LP, 1, 87),
         DK(70, 10, 0, 0), M0, DO(SR_26K, 2, 3, 90, 0)},                                 /* KICK */
        {DT(W_SINE, 29, 5, 30, 52, 120), B0, N0, DF(F_LP, 1, 84),
         DK(25, 6, 0, 0), M0, DO(SR_26K, 2, 2, 96, 0)},                                  /* 808 */
        {DT(W_SINE, 57, 3, 16, 24, 70), DB(W_SINE, 410, 40, 0), DN(N_WHITE, 0, 150, 34), DF(F_BP, 2, 98),
         DK(150, 10, 0, 0), M0, DO(SR_26K, 2, 5, 128, 0)},                                /* SNARE */
        {DT(W_SINE, 77, 0, 0, 16, 100), DB(W_SINE, 937, 80, 12), N0, DF(F_BP, 5, 96),
         DK(60, 6, 0, 0), M0, DO(SR_26K, 2, 6, 128, 0)},                                  /* RIMSHOT */
        {DT(W_SQUARE, 101, 0, 0, 16, 20), DB(W_SQUARE | B_RING, 379, 128, 0), DN(N_WHITE, 0, 120, 21), DF(F_BP, 1, 116),
         DK(60, 8, 0, 0), M0, DO(SR_26K, 3, 0, 128, DR_HAT | DR_TFILT)},                  /* CLOSED HAT, dusty */
        {DT(W_SQUARE, 101, 0, 0, 34, 20), DB(W_SQUARE | B_RING, 379, 128, 0), DN(N_WHITE, 0, 120, 37), DF(F_BP, 1, 115),
         DK(50, 8, 0, 0), M0, DO(SR_26K, 3, 0, 128, DR_HAT | DR_TFILT)},                  /* OPEN HAT */
        {DT(W_SINE, 45, 5, 30, 38, 115), DB(W_SINE, 407, 30, 30), DN(N_WHITE, 0, 14, 24), DF(F_LP, 1, 86),
         DK(40, 10, 0, 0), M0, DO(SR_26K, 2, 2, 128, 0)},                                 /* LOW TOM */
        {DT(W_SINE, 52, 5, 30, 36, 115), DB(W_SINE, 407, 30, 28), DN(N_WHITE, 0, 14, 22), DF(F_LP, 1, 89),
         DK(40, 10, 0, 0), M0, DO(SR_26K, 2, 2, 128, 0)},                                 /* HIGH TOM */
        {DT(W_SQUARE, 92, 0, 0, 44, 30), DB(W_SQUARE | B_RING, 443, 128, 0), DN(N_WHITE, 0, 120, 46), DF(F_BP, 0, 110),
         DK(70, 14, 0, 0), M0, DO(SR_26K, 3, 1, 128, DR_TFILT)},                          /* CRASH, dark */
        {T0, B0, DN(N_WHITE, 0, 50, 22), DF(F_BP, 5, 96), DK(200, 10, 1, 8), M0, DO(SR_26K, 2, 2, 128, 0)},   /* SNAP */
        {T0, B0, DN(N_WHITE, 0, 190, 24), DF(F_HP, 2, 113), K0, DM(18, 0, 0), DO(SR_26K, 2, 0, 128, 0)},      /* SHAKER */
        {T0, B0, DN(MTL(2), 84, 140, 34), DF(F_HP, 1, 112), DK(90, 12, 2, 40), M0,
         DO(SR_26K, 2, 0, 128, 0)},                                                       /* TAMBOURINE */
        {DT(W_SINE, 23, 0, 0, 52, 20), B0, DN(N_CRACKLE, 1, 255, 52), DF(F_BP, 2, 100), K0, M0,
         DO(SR_26K, 3, 0, 128, 0)},                                                       /* VINYL */
        {DT(W_SAW, 62, 0, 0, 36, 90), B0, DN(N_WHITE, 0, 30, 30), DF(F_BP, 4, 86), K0, DM(14, 160, 24),
         DO(SR_26K, 2, 2, 128, DR_TFILT)},                                                /* SCRATCH */
        {DT(W_SINE, 36, 16, 16, 32, 120), B0, N0, DF(F_LP, 1, 96),
         DK(90, 8, 0, 0), M0, DO(SR_26K, 2, 3, 90, 0)},                                  /* KNOCK KICK */
        {T0, B0, DN(N_WHITE, 0, 130, 33), DF(F_BP, 3, 85), DK(220, 10, 3, 22), M0,
         DO(SR_26K, 2, 1, 128, DR_TAIL)},                                                 /* CLAP */
    },
    {   /* TECHNO: a 909-ish kick and its hard brother, the 909's snare and clap, metallic hats and cymbals (six
         * squares), a rumble, the 808's cowbell and clave, an FM blip, a zap */
        {DT(W_SINE, 31, 24, 14, 38, 115), DB(W_TRI, 2048, 40, 10), N0, DF(F_LP, 1, 112),
         DK(90, 6, 0, 0), M0, DO(SR_OFF, 0, 2, 128, 0)},                                  /* KICK */
        {DT(W_SINE, 33, 36, 14, 43, 100), DB(W_SINE, 512, 40, 26), N0, DF(F_LP, 1, 108),
         DK(80, 8, 0, 0), M0, DO(SR_OFF, 0, 7, 44, 0)},                                  /* HARD KICK, distorted */
        {DT(W_SINE, 54, 4, 18, 26, 75), DB(W_SINE, 461, 55, 0), DN(N_WHITE, 0, 150, 34), DF(F_HP, 1, 99),
         DK(90, 8, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                  /* SNARE */
        {T0, B0, DN(N_WHITE, 0, 150, 34), DF(F_BP, 3, 86), DK(255, 10, 3, 28), M0,
         DO(SR_OFF, 0, 1, 128, DR_TAIL)},                                                 /* CLAP */
        {T0, B0, DN(MTL(0), 56, 200, 22), DF(F_HP, 1, 118), DK(40, 6, 0, 0), M0,
         DO(SR_OFF, 0, 1, 128, DR_HAT)},                                                  /* CLOSED HAT */
        {T0, B0, DN(MTL(0), 56, 190, 40), DF(F_HP, 1, 116), DK(30, 6, 0, 0), M0,
         DO(SR_OFF, 0, 1, 128, DR_HAT)},                                                  /* OPEN HAT */
        {DT(W_TRI, 45, 14, 26, 36, 115), B0, DN(N_WHITE, 0, 30, 20), DF(F_LP, 1, 96),
         DK(30, 8, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                  /* LOW TOM, synth */
        {DT(W_TRI, 52, 14, 26, 34, 115), B0, DN(N_WHITE, 0, 30, 18), DF(F_LP, 1, 99),
         DK(30, 8, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                  /* HIGH TOM, synth */
        {DT(W_SINE, 100, 0, 0, 44, 30), DB(W_SINE | B_FM, 361, 60, 34), DN(MTL(3), 74, 170, 48), DF(F_BP, 3, 112),
         DK(40, 8, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                  /* RIDE */
        {T0, B0, DN(MTL(1), 69, 200, 50), DF(F_HP, 1, 112), DK(90, 16, 0, 0), M0,
         DO(SR_OFF, 0, 1, 128, 0)},                                                       /* CRASH */
        {DT(W_TRI, 88, 0, 0, 12, 110), DB(W_SINE, 832, 80, 10), N0, DF(F_BP, 4, 103),
         DK(80, 6, 0, 0), M0, DO(SR_OFF, 0, 4, 128, 0)},                                  /* RIM */
        {DT(W_SINE, 31, 0, 0, 46, 150), B0, DN(N_WHITE, 0, 160, 46), DF(F_LP, 2, 47), K0, DM(32, 6, 2),
         DO(SR_OFF, 0, 2, 128, 0)},                                                       /* RUMBLE */
        {DT(W_SQUARE, 73, 0, 0, 32, 90), DB(W_SQUARE, 379, 90, 0), N0, DF(F_BP, 3, 80), K0, M0,
         DO(SR_OFF, 0, 1, 128, DR_TFILT)},                                                /* COWBELL */
        {DT(W_SINE, 72, 0, 0, 24, 110), DB(W_SINE | B_FM, 896, 110, 16), N0, F0, K0, M0,
         DO(SR_OFF, 0, 0, 128, 0)},                                                       /* FM BLIP */
        {DT(W_SINE, 99, 0, 0, 20, 130), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 2, 128, 0)},  /* CLAVE */
        {DT(W_SQUARE, 36, 48, 22, 30, 90), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, 0)},   /* ZAP */
    },
    {   /* DUB REGGAE: round kicks, a springy snare (its band chirps down), the side stick, deep toms, a shaker,
         * a syndrum, the siren, a splash, a wood block, the nyabinghi's funde and repeater, a laser */
        {DT(W_SINE, 30, 5, 34, 44, 125), B0, N0, DF(F_LP, 0, 70),
         K0, M0, DO(SR_OFF, 0, 1, 120, 0)},                                               /* KICK, round and deep */
        {DT(W_SINE, 29, 10, 34, 42, 125), DB(W_SINE, 512, 20, 34), N0, DF(F_LP, 1, 79),
         DK(20, 10, 0, 0), M0, DO(SR_OFF, 0, 2, 128, 0)},                                 /* STEPPERS KICK */
        {DT(W_SINE, 62, 7, 32, 30, 85), DB(W_SINE, 384, 50, 0), DN(N_WHITE, 0, 130, 38), DF(F_BP, 9, 103),
         DK(90, 8, 0, 0), M0, DO(SR_OFF, 0, 1, 128, DR_FSWEEP)},                          /* SNARE, springy */
        {DT(W_SINE, 76, 0, 0, 14, 120), DB(W_TRI, 671, 70, 12), N0, DF(F_BP, 5, 96),
         DK(90, 6, 0, 0), M0, DO(SR_OFF, 0, 3, 128, 0)},                                  /* SIDE STICK */
        {T0, B0, DN(N_WHITE, 0, 150, 18), DF(F_HP, 0, 118), DK(50, 6, 0, 0), M0,
         DO(SR_OFF, 0, 0, 128, DR_HAT)},                                                  /* CLOSED HAT */
        {T0, B0, DN(N_WHITE, 0, 140, 38), DF(F_HP, 1, 114), K0, DM(6, 0, 0),
         DO(SR_OFF, 0, 0, 128, DR_HAT)},                                                  /* OPEN HAT */
        {DT(W_SINE, 40, 5, 36, 42, 125), DB(W_SINE, 384, 30, 36), N0, DF(F_LP, 1, 84),
         DK(30, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                 /* LOW TOM, deep */
        {DT(W_SINE, 47, 5, 36, 40, 125), DB(W_SINE, 384, 30, 34), N0, DF(F_LP, 1, 87),
         DK(30, 10, 0, 0), M0, DO(SR_OFF, 0, 0, 128, 0)},                                 /* HIGH TOM */
        {T0, B0, DN(N_WHITE, 0, 200, 26), DF(F_BP, 3, 115), DK(0, 0, 1, 50), DM(16, 0, 0),
         DO(SR_OFF, 0, 0, 128, DR_HTONE)},                                                /* SHAKER */
        {DT(W_TRI, 57, 12, 38, 44, 120), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 1, 128, 0)},  /* SYNDRUM */
        {DT(W_TRI, 72, -12, 40, 50, 110), B0, N0, F0, K0, DM(22, 112, 20), DO(SR_OFF, 0, 0, 128, 0)},   /* SIREN */
        {DT(W_SQUARE, 96, 0, 0, 40, 30), DB(W_SQUARE | B_RING, 443, 128, 0), DN(N_WHITE, 0, 140, 42), DF(F_HP, 0, 108),
         DK(80, 12, 0, 0), M0, DO(SR_OFF, 0, 0, 128, DR_TFILT)},                          /* SPLASH */
        {DT(W_SINE, 84, 0, 0, 16, 120), DB(W_SINE, 538, 40, 12), N0, DF(F_BP, 8, 84),
         DK(60, 6, 0, 0), M0, DO(SR_OFF, 0, 2, 128, 0)},                                  /* WOOD BLOCK */
        {DT(W_SINE, 50, 3, 28, 34, 115), DB(W_SINE, 407, 30, 26), N0, DF(F_BP, 3, 91),
         DK(70, 8, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                  /* FUNDE */
        {DT(W_SINE, 59, 4, 24, 30, 115), DB(W_SINE, 407, 40, 24), N0, DF(F_BP, 3, 99),
         DK(110, 8, 0, 0), M0, DO(SR_OFF, 0, 2, 128, 0)},                                 /* REPEATER */
        {DT(W_TRI, 60, 36, 30, 38, 110), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, 0)},  /* LASER */
    },
    {   /* LO-FI: soft and band-limited (16 / 11 kHz, 10 / 8 bits), tape-saturated, the pitch wobbling a little,
         * a different way each hit */
        {DT(W_SINE, 36, 10, 22, 32, 125), B0, DN(N_CRACKLE, 40, 30, 30), DF(F_LP, 1, 76), DK(60, 10, 0, 0),
         DM(0, 4, 3), DO(SR_11K, 4, 3, 90, DR_WRND)},                                    /* KICK, dusty */
        {DT(W_TRI, 31, 5, 30, 34, 120), B0, DN(N_WHITE, 0, 60, 26), DF(F_LP, 1, 55), K0, DM(0, 4, 3),
         DO(SR_16K, 3, 4, 90, DR_WRND)},                                                 /* THUMP */
        {DT(W_SINE, 55, 2, 20, 26, 60), DB(W_SINE, 410, 30, 0), DN(N_WHITE, 115, 150, 32), DF(F_LP, 2, 104),
         DK(60, 12, 0, 0), DM(0, 4, 3), DO(SR_11K, 4, 3, 128, DR_WRND)},                  /* SNARE */
        {DT(W_SINE, 74, 0, 0, 14, 110), DB(W_SINE, 742, 40, 10), N0, DF(F_BP, 4, 93),
         DK(40, 6, 0, 0), DM(0, 4, 3), DO(SR_11K, 4, 3, 128, DR_WRND)},                   /* RIM */
        {T0, B0, DN(N_WHITE, 0, 140, 20), DF(F_BP, 2, 112), DK(40, 8, 0, 0), M0,
         DO(SR_11K, 4, 1, 128, DR_HAT)},                                                  /* CLOSED HAT */
        {T0, B0, DN(N_WHITE, 0, 130, 36), DF(F_BP, 2, 111), K0, M0, DO(SR_11K, 4, 1, 128, DR_HAT)},   /* OPEN HAT */
        {DT(W_SINE, 45, 3, 34, 36, 120), B0, N0, DF(F_LP, 1, 84), DK(30, 10, 0, 0), DM(0, 6, 3),
         DO(SR_16K, 3, 2, 90, DR_WRND)},                                                 /* TOM */
        {DT(W_SINE, 52, 3, 34, 34, 120), B0, N0, DF(F_LP, 1, 87), DK(30, 10, 0, 0), DM(0, 6, 3),
         DO(SR_16K, 3, 2, 128, DR_WRND)},                                                 /* TOM 2 */
        {T0, B0, DN(N_WHITE, 0, 40, 22), DF(F_BP, 5, 94), DK(220, 10, 1, 6), M0, DO(SR_11K, 4, 2, 128, 0)},   /* SNAP */
        {T0, B0, DN(N_WHITE, 0, 200, 28), DF(F_BP, 2, 107), K0, DM(20, 0, 0), DO(SR_11K, 4, 0, 128, 0)},      /* SHAKER */
        {DT(W_SINE, 84, 0, 0, 46, 90), DB(W_SINE | B_FM, 896, 50, 30), N0, F0, K0, DM(0, 8, 4),
         DO(SR_16K, 3, 1, 128, DR_WRND)},                                                 /* TING */
        {DT(W_SINE, 24, 0, 0, 52, 25), B0, DN(N_CRACKLE, 1, 255, 52), DF(F_BP, 2, 98), K0, M0,
         DO(SR_11K, 3, 0, 128, 0)},                                                       /* VINYL */
        {T0, B0, DN(N_WHITE, 0, 200, 36), DF(F_LP, 1, 100), K0, DM(28, 0, 0), DO(SR_11K, 4, 0, 128, 0)},      /* BRUSH */
        {T0, B0, DN(N_WHITE, 0, 100, 30), DF(F_BP, 3, 86), DK(200, 10, 2, 25), M0,
         DO(SR_11K, 4, 2, 128, DR_TAIL)},                                                 /* CLAP */
        {DT(W_SINE, 67, 0, 0, 20, 115), DB(W_SINE, 589, 50, 14), N0, DF(F_BP, 7, 88), DK(50, 6, 0, 0),
         DM(0, 4, 3), DO(SR_11K, 4, 3, 128, DR_WRND)},                                    /* KNOCK */
        {DT(W_SAW, 36, 36, 42, 42, 90), B0, N0, DF(F_LP, 1, 96), K0, M0,
         DO(SR_16K, 4, 2, 128, DR_TFILT)},                                                /* TAPE DROP */
    },
    {   /* GAME BOY: the 4-bit triangle and the pulses swept in whole semitones, the LFSR's noise (15 bits:
         * hiss; 7 bits: a buzz), 16 volume steps, no filter: hard edges */
        {DT(W_GB4, 36, 36, 22, 30, 130), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},     /* KICK */
        {DT(W_SQUARE, 33, 30, 20, 26, 90), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},   /* PULSE KICK */
        {DT(W_GB4, 57, 12, 14, 18, 80), B0, DN(N_LFSR15, 0, 120, 30), F0, K0, M0,
         DO(SR_OFF, 0, 0, 128, DR_GB)},                                                   /* SNARE */
        {DT(W_GB4, 52, 12, 14, 16, 60), B0, DN(N_LFSR7, 0, 140, 28), F0, K0, M0,
         DO(SR_OFF, 0, 0, 128, DR_GB)},                                                   /* SNARE, 7 bits */
        {T0, B0, DN(N_LFSR15, 0, 120, 16), F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB | DR_HAT)},   /* CLOSED HAT */
        {T0, B0, DN(N_LFSR15, 0, 120, 36), F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB | DR_HAT)},   /* OPEN HAT */
        {DT(W_PULSE, 48, 12, 26, 30, 100), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},   /* TOM */
        {DT(W_PULSE, 55, 12, 26, 28, 100), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},   /* TOM 2 */
        {T0, B0, DN(N_LFSR15, 124, 130, 46), F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},         /* CRASH */
        {T0, B0, DN(N_LFSR7, 131, 130, 34), F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},          /* BUZZ */
        {DT(W_PULSE12, 84, 0, 0, 22, 100), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},   /* BLIP */
        {DT(W_SQUARE, 48, 48, 20, 30, 90), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},   /* ZAP */
        {T0, B0, DN(N_LFSR15, 108, 160, 48), F0, K0, M0, DO(SR_OFF, 0, 0, 100, DR_GB)},         /* EXPLOSION */
        {DT(W_PULSE, 60, -24, 34, 40, 90), B0, N0, F0, K0, M0, DO(SR_OFF, 0, 0, 128, DR_GB)},   /* JUMP */
        {DT(W_PULSE12, 88, -5, 14, 42, 100), B0, N0, F0, K0, M0,
         DO(SR_OFF, 0, 0, 128, DR_GB | DR_STEP)},                                         /* COIN: B5, then E6 */
        {T0, B0, DN(N_LFSR15, 0, 110, 30), F0, DK(200, 10, 3, 22), M0,
         DO(SR_OFF, 0, 0, 128, DR_GB | DR_TAIL)},                                         /* CLAP */
    },
    {   /* SNES: bright and sample-like through 32 kHz and 12 bits: punchy kicks, crisp snares, timpani, an
         * orchestra hit, a glockenspiel, a taiko, a steel drum */
        {DT(W_SINE, 34, 19, 18, 36, 115), DB(W_SINE, 768, 30, 20), N0, DF(F_BP, 2, 103),
         DK(110, 8, 0, 0), M0, DO(SR_32K, 2, 2, 128, 0)},                                 /* KICK */
        {DT(W_SINE, 31, 24, 20, 40, 120), DB(W_SINE, 512, 30, 30), N0, DF(F_BP, 2, 96),
         DK(90, 10, 0, 0), M0, DO(SR_32K, 2, 4, 90, 0)},                                 /* POWER KICK */
        {DT(W_SINE, 59, 5, 16, 24, 70), DB(W_SINE, 435, 40, 0), DN(N_WHITE, 0, 170, 32), DF(F_HP, 1, 105),
         DK(150, 8, 0, 0), M0, DO(SR_32K, 2, 2, 128, 0)},                                 /* SNARE */
        {DT(W_SINE, 64, 2, 14, 22, 60), DB(W_SINE, 410, 40, 0), DN(N_WHITE, 0, 170, 30), DF(F_BP, 2, 114),
         DK(150, 8, 2, 14), M0, DO(SR_32K, 2, 1, 128, 0)},                                /* ORCHESTRA SNARE */
        {DT(W_SQUARE, 105, 0, 0, 16, 25), DB(W_SQUARE | B_RING, 361, 128, 0), DN(N_WHITE, 0, 160, 18), DF(F_HP, 0, 118),
         DK(60, 8, 0, 0), M0, DO(SR_32K, 2, 0, 128, DR_HAT | DR_TFILT)},                  /* CLOSED HAT */
        {DT(W_SQUARE, 105, 0, 0, 36, 25), DB(W_SQUARE | B_RING, 361, 128, 0), DN(N_WHITE, 0, 150, 38), DF(F_HP, 0, 117),
         DK(50, 8, 0, 0), M0, DO(SR_32K, 2, 0, 128, DR_HAT | DR_TFILT)},                  /* OPEN HAT */
        {DT(W_SINE, 41, 1, 40, 50, 115), DB(W_SINE, 384, 50, 46), N0, DF(F_LP, 1, 72),
         DK(50, 12, 0, 0), DM(4, 0, 0), DO(SR_32K, 2, 0, 128, 0)},                        /* TIMPANI */
        {DT(W_SINE, 48, 1, 40, 48, 115), DB(W_SINE, 384, 50, 44), N0, DF(F_LP, 1, 76),
         DK(50, 12, 0, 0), DM(4, 0, 0), DO(SR_32K, 2, 0, 128, 0)},                        /* TIMPANI, high */
        {T0, B0, DN(MTL(1), 70, 200, 50), DF(F_HP, 1, 114), DK(100, 14, 0, 0), M0,
         DO(SR_32K, 2, 0, 128, 0)},                                                       /* CRASH */
        {DT(W_SAW, 60, 0, 0, 38, 100), DB(W_SAW, 384, 100, 0), DN(N_WHITE, 0, 60, 24), DF(F_LP, 1, 107), K0, M0,
         DO(SR_32K, 2, 2, 128, DR_TFILT)},                                                /* ORCHESTRA HIT */
        {DT(W_SINE, 47, 6, 30, 36, 120), DB(W_SINE, 407, 30, 30), N0, DF(F_BP, 2, 98),
         DK(70, 8, 0, 0), M0, DO(SR_32K, 2, 1, 128, 0)},                                  /* TOM */
        {DT(W_SINE, 54, 6, 30, 34, 120), DB(W_SINE, 407, 30, 28), N0, DF(F_BP, 2, 101),
         DK(70, 8, 0, 0), M0, DO(SR_32K, 2, 1, 128, 0)},                                  /* TOM 2 */
        {DT(W_SINE, 96, 0, 0, 46, 90), DB(W_SINE | B_FM, 896, 70, 30), N0, F0, K0, M0,
         DO(SR_32K, 2, 0, 128, 0)},                                                       /* GLOCKENSPIEL */
        {DT(W_SINE, 36, 7, 30, 42, 130), B0, DN(N_WHITE, 0, 40, 30), DF(F_BP, 3, 79),
         DK(90, 12, 0, 0), M0, DO(SR_32K, 2, 2, 90, 0)},                                 /* TAIKO */
        {T0, B0, DN(N_WHITE, 0, 140, 32), DF(F_BP, 2, 90), DK(230, 10, 2, 24), M0,
         DO(SR_32K, 2, 1, 128, DR_TAIL)},                                                 /* CLAP */
        {DT(W_SINE, 74, 0, 0, 40, 100), DB(W_SINE | B_FM, 512, 50, 34), N0, F0, K0, M0,
         DO(SR_32K, 2, 0, 128, 0)},                                                       /* STEEL DRUM */
    },
    {   /* PERCU: congas (low, high, the slap), a timbale, a shaker, a tambourine, bongos, claves, a cowbell,
         * agogo bells, a guiro (its ridges struck one by one), a triangle, a wood block, a cabasa */
        {DT(W_SINE, 53, 2, 26, 34, 125), DB(W_SINE, 407, 30, 26), N0, DF(F_LP, 1, 89),
         DK(50, 10, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                 /* CONGA, low */
        {DT(W_SINE, 61, 2, 24, 30, 125), DB(W_SINE, 407, 30, 24), N0, DF(F_BP, 3, 98),
         DK(60, 8, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                  /* CONGA, high */
        {DT(W_SINE, 60, 0, 0, 18, 70), B0, DN(N_WHITE, 0, 80, 22), DF(F_BP, 4, 101),
         DK(200, 12, 0, 0), M0, DO(SR_OFF, 0, 3, 128, 0)},                                /* SLAP */
        {DT(W_SINE, 67, 1, 30, 38, 100), DB(W_SINE | B_RING, 699, 60, 30), N0, DF(F_BP, 2, 107),
         DK(120, 8, 0, 0), M0, DO(SR_OFF, 0, 1, 128, 0)},                                 /* TIMBALE */
        {T0, B0, DN(N_WHITE, 0, 220, 24), DF(F_BP, 3, 115), K0, DM(16, 0, 0), DO(SR_OFF, 0, 0, 128, 0)},   /* SHAKER */
        {T0, B0, DN(MTL(2), 86, 180, 40), DF(F_HP, 1, 112), DK(120, 10, 2, 30), M0,
         DO(SR_OFF, 0, 0, 128, 0)},                                                       /* TAMBOURINE */
        {DT(W_SINE, 66, 3, 20, 26, 125), B0, N0, DF(F_BP, 3, 104), DK(70, 6, 0, 0), M0,
         DO(SR_OFF, 0, 1, 128, 0)},                                                       /* BONGO, low */
        {DT(W_SINE, 73, 3, 20, 24, 125), B0, N0, DF(F_BP, 3, 107), DK(70, 6, 0, 0), M0,
         DO(SR_OFF, 0, 1, 128, 0)},                                                       /* BONGO, high */
        {DT(W_SINE, 98, 0, 0, 22, 140), DB(W_SINE, 351, 20, 18), N0, F0, K0, M0, DO(SR_OFF, 0, 1, 128, 0)},   /* CLAVES */
        {DT(W_PULSE, 69, 0, 0, 34, 110), DB(W_SQUARE, 379, 100, 0), N0, DF(F_BP, 2, 86), DK(60, 6, 0, 0), M0,
         DO(SR_OFF, 0, 1, 128, DR_TFILT)},                                                /* COWBELL */
        {DT(W_SINE, 79, 0, 0, 42, 120), DB(W_SINE | B_FM, 691, 40, 26), N0, DF(F_HP, 1, 100), DK(40, 6, 0, 0), M0,
         DO(SR_OFF, 0, 0, 128, 0)},                                                       /* AGOGO, low */
        {DT(W_SINE, 84, 0, 0, 40, 120), DB(W_SINE | B_FM, 691, 40, 26), N0, DF(F_HP, 1, 104), DK(40, 6, 0, 0), M0,
         DO(SR_OFF, 0, 0, 128, 0)},                                                       /* AGOGO, high */
        {T0, B0, DN(N_WHITE, 0, 30, 36), DF(F_BP, 6, 103), DK(200, 8, 14, 30), M0,
         DO(SR_OFF, 0, 1, 128, 0)},                                                       /* GUIRO */
        {DT(W_SINE, 93, 0, 0, 52, 90), DB(W_SINE | B_FM, 955, 30, 40), N0, DF(F_HP, 1, 117), DK(40, 6, 0, 0), M0,
         DO(SR_OFF, 0, 0, 128, 0)},                                                       /* TRIANGLE */
        {DT(W_SINE, 83, 0, 0, 18, 140), DB(W_SINE, 666, 40, 12), N0, DF(F_BP, 8, 89), DK(70, 4, 0, 0), M0,
         DO(SR_OFF, 0, 1, 128, 0)},                                                       /* WOOD BLOCK */
        {T0, B0, DN(N_WHITE, 0, 230, 30), DF(F_HP, 2, 116), DK(0, 0, 1, 20), DM(12, 0, 0),
         DO(SR_OFF, 0, 0, 128, DR_HTONE)},                                                /* CABASA */
    }};
#undef DT
#undef DB
#undef DN
#undef DF
#undef DK
#undef DM
#undef DO
#undef T0
#undef B0
#undef N0
#undef F0
#undef K0
#undef M0
#undef MTL
