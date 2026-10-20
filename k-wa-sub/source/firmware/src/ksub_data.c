/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: the patch (its parameters, their names, ranges and formats), the lists, the factory presets. */

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- the parameters of a patch (int8 each; the order is the stored order: append only) */
enum {
    A_ENG, A_LVL, A_P1, A_P2, A_P3, A_OCT, A_SEMI, A_FINE,          /* oscillator A */
    B_ENG, B_LVL, B_P1, B_P2, B_P3, B_OCT, B_SEMI, B_FINE,          /* oscillator B (same layout: +8) */
    S_WAVE, S_OCT, S_LVL, S_ROUTE, N_LVL, N_COL,                    /* sub, noise */
    X_MODE, X_AMT, X_DRIFT, X_RESET,                                /* how A and B combine */
    F_TYPE, F_CUT, F_RES, F_DRV, F_ENV, F_KEY, F_VEL,               /* filter */
    AE_A, AE_D, AE_S, AE_R, FE_A, FE_D, FE_S, FE_R,                 /* amp and filter envelopes */
    ME_A, ME_D, ME_AMT, ME_DST,                                     /* mod envelope (AD) */
    L1_SHP, L1_SYNC, L1_RATE, L1_DIV, L1_DEP, L1_DST, L1_TRIG, L1_PH, L1_SMO,   /* LFO 1 (the wobble) */
    L2_SHP, L2_SYNC, L2_RATE, L2_DIV, L2_DEP, L2_DST, L2_TRIG, L2_PH, L2_SMO,   /* LFO 2 (same layout: +9) */
    M1_SRC, M1_DST, M1_AMT, M2_SRC, M2_DST, M2_AMT, M3_SRC, M3_DST, M3_AMT, M4_SRC, M4_DST, M4_AMT,
    DR_ON, DR_TYPE, DR_AMT, DR_MIX, CR_ON, CR_BITS, CR_RATE, CR_MIX, CH_ON, CH_RATE, CH_DEP, CH_MIX,
    DL_ON, DL_TIME, DL_FB, DL_TONE, DL_MIX, RV_ON, RV_SIZE, RV_DAMP, RV_MIX, MS_GLUE, MS_SUB, MS_VOL,
    V_MODE, V_GLIDE, V_BEND, V_VEL, P_STYLE,
    ST_ON, ST_PAT, ST_RATE, ST_GATE, ST_DST, ST_DEP, ST_SMO,     /* the STEP effect: 16 steps on the tempo */
    NP
};
#define NP_MAX 112u                             /* room in a stored patch */
#define OSC_B (B_ENG - A_ENG)
#define LFO_2 (L2_SHP - L1_SHP)
#define NENG 8
enum { E_ANALOG, E_SUPER, E_WAVE, E_FM, E_PHASE, E_FOLD, E_SYNC, E_VOCAL };
enum { FT_LP24, FT_LP12, FT_BP, FT_HP, FT_NOTCH, FT_VOWEL, FT_COMB, NFT };
enum { D_OFF, D_CUT, D_RES, D_PITCH, D_AP1, D_AP2, D_BP1, D_BP2, D_ALVL, D_BLVL, D_XMOD, D_DRV, D_AMP, D_SUB, D_NOISE,
       D_WOB, D_AP3, D_BP3, D_APIT, D_BPIT, D_NCOL, D_FENV, D_L2DEP,
       D_L1RATE, D_L2RATE, D_GATE, D_DRAMT, D_DRMIX, D_CRBITS, D_CRRATE, D_CRMIX, D_CHRATE, D_CHDEP, D_CHMIX,   /* global: */
       D_DLFB, D_DLTONE, D_DLMIX, D_RVSIZE, D_RVDAMP, D_RVMIX, D_GLUE, D_SUBB, D_VOL,   /* once a block, not a voice */
       ND };   /* (stored in patches: append only) */
#define IS_PITCH(d) ((d) == D_PITCH || (d) == D_APIT || (d) == D_BPIT)
/* the order DEST steps through: the oscillators' EDIT rows together (ksub_ui.c skips an oscillator that is off) */
static const uint8_t DST_ORDER[ND] = {D_OFF, D_CUT, D_RES, D_DRV, D_FENV, D_PITCH, D_ALVL, D_AP1, D_AP2, D_AP3, D_APIT, D_BLVL,
                                      D_BP1, D_BP2, D_BP3, D_BPIT, D_XMOD, D_SUB, D_NOISE, D_NCOL, D_AMP, D_WOB, D_L1RATE,
                                      D_L2RATE, D_L2DEP, D_GATE, D_DRAMT, D_DRMIX, D_CRBITS, D_CRRATE, D_CRMIX, D_CHRATE,
                                      D_CHDEP, D_CHMIX, D_DLFB, D_DLTONE, D_DLMIX, D_RVSIZE, D_RVDAMP, D_RVMIX, D_GLUE, D_SUBB,
                                      D_VOL};
/* the parameter each global destination moves */
#define D_G0 D_L1RATE

enum { SR_OFF, SR_LFO1, SR_LFO2, SR_FENV, SR_MENV, SR_VEL, SR_WHEEL, SR_PRESS, SR_NOTE, SR_RAND, SR_STEP, NSRC };
enum { VM_MONO, VM_LEGATO, VM_POLY };

/* formats of the values on the screen */
enum { FM_PCT, FM_BIP, FM_LIST, FM_INT, FM_SEMI, FM_CENT, FM_TIME, FM_CUT, FM_LFOHZ, FM_DIV, FM_VOL, FM_OSCP, FM_DEST };

static const char *const L_ENG[NENG] = {"ANALOG", "SUPER", "WAVE", "FM", "PHASE", "FOLD", "SYNC", "VOCAL"};
static const char *const L_SUBW[] = {"SINE", "TRI", "SQUARE", "PULSE"};
static const char *const L_SUBO[] = {"-2 OCT", "-1 OCT", "0 OCT"};
static const char *const L_ROUTE[] = {"FILTER", "DIRECT"};
static const char *const L_XMODE[] = {"ADD", "RING", "X-MOD"};
static const char *const L_FT[NFT] = {"LP 24", "LP 12", "BAND", "HIGH", "NOTCH", "VOWEL", "COMB"};
static const char *const L_SHAPE[] = {"SINE", "TRI", "SAW DN", "SAW UP", "SQUARE", "S&H", "WUB", "STEPS", "DRIFT"};
static const char *const L_DIV[16] = {"4 BAR", "2 BAR", "1 BAR", "1/2", "1/4D", "1/2T", "1/4", "1/8D", "1/4T", "1/8", "1/8T",
                                      "1/16", "1/16T", "1/32", "1/32T", "1/64"};
static const uint32_t DIV_CPB[16] = {4096, 8192, 16384, 32768, 43691, 49152, 65536, 87381, 98304, 131072, 196608,
                                     262144, 393216, 524288, 786432, 1048576};   /* cycles per beat, Q16 */
static const char *const L_DST[ND] = {"OFF", "CUTOFF", "RESO", "PITCH", "A SHAPE", "A COLOR", "B SHAPE", "B COLOR",
                                      "A LEVEL", "B LEVEL", "X-MOD", "FILT DRIVE", "AMP", "SUB", "NOISE", "WOB DEPTH",
                                      "A EDGE", "B EDGE", "A PITCH", "B PITCH", "NOISE COLOR", "FILT ENV", "LFO2 DEPTH",
                                      "WOB RATE", "LFO2 RATE", "STEP GATE", "DRIVE AMT", "DRIVE MIX", "CRUSH BITS",
                                      "CRUSH RATE", "CRUSH MIX", "CHORUS RATE", "CHORUS DEPTH", "CHORUS MIX", "DELAY FDBK",
                                      "DELAY TONE", "DELAY MIX", "VERB SIZE", "VERB DAMP", "VERB MIX", "GLUE", "SUB BOOST",
                                      "VOLUME"};
static const char *const L_SRC[NSRC] = {"OFF", "LFO 1", "LFO 2", "FILT ENV", "MOD ENV", "VELOCITY", "MOD WHEEL",
                                        "PRESSURE", "NOTE", "RANDOM", "STEP"};
/* the STEP patterns: 16 steps, a level 0..F each ("RANDOM": a new one every bar) */
static const char *const L_SPAT[16] = {"OFFBEAT", "PUMP", "DUB CHOP", "TRANCE", "TRIPLET", "BROKEN", "SKANK", "GALLOP",
                                      "STAIRS UP", "STAIRS DN", "DNB ROLL", "HALFTIME", "STUTTER", "ACID", "SYNCOPE", "RANDOM"};
static const char *const STEP_PAT[16] = {"0F0F0F0F0F0F0F0F", "37BF37BF37BF37BF", "F0F00F00F0F00F00", "F0FF0FF0F0FF0FF0",
                                        "F00F00F00F00F00F", "F00F00F0F00F0F00", "00F000F000F000F0", "F0FFF0FFF0FFF0FF",
                                        "0123456789ABCDEF", "FEDCBA9876543210", "F0F0FFF0F0F0FF0F", "FFFF0000FFFF0000",
                                        "FFF0F0F0FFFF0000", "F8A3F5C1E07B92D4", "F00F0F00F00F0F00", "0000000000000000"};
static const char *const L_SRATE[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const uint8_t STEP_PB[6] = {1, 2, 3, 4, 6, 8};   /* steps a beat */
static const char *const L_TRIG[] = {"FREE", "NOTE"};
static const char *const L_ONOFF[] = {"OFF", "ON"};
static const char *const L_DRT[] = {"SOFT", "TUBE", "HARD", "FOLD"};
static const char *const L_VM[] = {"MONO", "LEGATO", "POLY"};
static const char *const L_STYLE[] = {"DNB", "DUB", "DUBSTEP", "TECHNO", "JUNGLE", "UK GARAGE", "TRAP", "HOUSE", "NEUROFUNK",
                                      "DUB TECHNO", "BREAKS", "MINIMAL"};   /* the music a preset is for */
#define NSTYLE 12

/* the engines' three timbre controls: names (long, short) and how they read */
static const char *const ENG_P[NENG][3] = {
    {"WAVE", "PWIDTH", "DRIVE"}, {"DETUNE", "MIX", "VOICES"}, {"POSITION", "TABLE", "WARP"},
    {"INDEX", "RATIO", "FEEDBACK"}, {"AMOUNT", "TYPE", "BODY"}, {"FOLD", "SYMMETRY", "SHAPE"},
    {"SYNC", "WAVE", "SOFT"}, {"VOWEL", "FORMANT", "WIDTH"}};
static const char *const ENG_TITLE[NENG] = {"ANALOG / VA", "SUPER / REESE", "WAVETABLE", "FM / GROWL",
                                            "PHASE DIST", "WAVEFOLDER", "HARD SYNC", "VOCAL / YOI"};
static const char *const ENG_SUB[NENG] = {"SUBTRACTIVE CORE", "DETUNED SAW STACK", "8 TABLES x 8 FRAMES",
                                          "2-OP FM + FEEDBACK", "CZ-STYLE PHASE DISTORTION", "WEST COAST FOLDING",
                                          "MASTER / SLAVE SYNC", "FORMANT TALKING BASS"};
static const char *const L_WT[8] = {"BASIC", "HARMONIC", "GROWL", "VOCAL", "FM", "PWM", "DIGITAL", "SYNC"};
static const char *const L_PDT[6] = {"SAW", "SQUARE", "PULSE", "RESO SAW", "RESO TRI", "RESO TRAP"};
static const char *const L_RATIO[16] = {"0.50", "1.00", "1.50", "2.00", "2.50", "3.00", "3.50", "4.00", "5.00", "6.00",
                                        "7.00", "8.00", "9.00", "11.0", "1.41", "3.14"};
static const uint16_t RATIO_Q8[16] = {128, 256, 384, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2304, 2816, 361, 804};
static const char *const L_VOX = "AEIOU";
static const uint8_t SUPER_N[4] = {2, 3, 5, 7};

typedef struct {
    const char *name, *sname;                   /* the row's name, the knob's (footer) */
    int8_t lo, hi, def;
    uint8_t fmt;
    const char *const *list;
} pdesc_t;

#define OSC_PD(L) \
    {L "ENGINE", "ENGINE", 0, NENG - 1, 0, FM_LIST, L_ENG}, {L "LEVEL", "LEVEL", 0, 127, 100, FM_PCT, 0}, \
    {L "P1", "P1", 0, 127, 85, FM_OSCP, 0}, {L "P2", "P2", 0, 127, 0, FM_OSCP, 0}, {L "P3", "P3", 0, 127, 0, FM_OSCP, 0}, \
    {L "OCTAVE", "OCT", -3, 2, 0, FM_INT, 0}, {L "SEMI", "SEMI", -12, 12, 0, FM_SEMI, 0}, \
    {L "FINE", "FINE", -50, 50, 0, FM_CENT, 0}
#define LFO_PD(L) \
    {"SHAPE", "SHAPE", 0, 8, 0, FM_LIST, L_SHAPE}, {"SYNC", "SYNC", 0, 1, 1, FM_LIST, L_ONOFF}, \
    {"RATE", "RATE", 0, 127, 70, FM_LFOHZ, 0}, {"RATE / SYNC", "RATE", 0, 15, 9, FM_DIV, L_DIV}, \
    {"DEPTH", "DEPTH", -64, 63, 0, FM_BIP, 0}, {"DEST", "DEST", 0, ND - 1, D_CUT, FM_DEST, L_DST}, \
    {"RETRIG", "TRIG", 0, 1, 0, FM_LIST, L_TRIG}, {"PHASE", "PHASE", 0, 127, 0, FM_PCT, 0}, \
    {"SMOOTH", "SMOOTH", 0, 127, 0, FM_PCT, 0}
#define MX_PD(N) \
    {N " SOURCE", "SRC" N, 0, NSRC - 1, 0, FM_LIST, L_SRC}, {N " DEST", "DST" N, 0, ND - 1, 0, FM_DEST, L_DST}, \
    {N " AMOUNT", "AMT" N, -64, 63, 0, FM_BIP, 0}

static const pdesc_t PD[NP] = {
    OSC_PD("A "), OSC_PD("B "),
    {"SUB WAVE", "WAVE", 0, 3, 0, FM_LIST, L_SUBW}, {"SUB OCTAVE", "OCT", 0, 2, 1, FM_LIST, L_SUBO},
    {"SUB LEVEL", "SUB", 0, 127, 0, FM_PCT, 0}, {"SUB ROUTE", "ROUTE", 0, 1, 1, FM_LIST, L_ROUTE},
    {"NOISE LEVEL", "NOISE", 0, 127, 0, FM_PCT, 0}, {"NOISE COLOR", "COLOR", 0, 127, 40, FM_PCT, 0},
    {"MODE", "MODE", 0, 2, 0, FM_LIST, L_XMODE}, {"X-MOD", "XMOD", 0, 127, 0, FM_PCT, 0},
    {"DRIFT", "DRIFT", 0, 127, 10, FM_PCT, 0}, {"KEY RESET", "RESET", 0, 1, 1, FM_LIST, L_ONOFF},
    {"TYPE", "TYPE", 0, NFT - 1, 0, FM_LIST, L_FT}, {"CUTOFF", "FREQ", 0, 127, 90, FM_CUT, 0},
    {"RESONANCE", "RES", 0, 127, 15, FM_PCT, 0}, {"DRIVE", "DRIVE", 0, 127, 0, FM_PCT, 0},
    {"ENV AMOUNT", "ENV", -64, 63, 0, FM_BIP, 0}, {"KEY TRACK", "KEY", 0, 127, 64, FM_PCT, 0},
    {"VELOCITY", "VEL", 0, 127, 0, FM_PCT, 0},
    {"01 ATTACK", "A", 0, 127, 0, FM_TIME, 0}, {"02 DECAY", "D", 0, 127, 70, FM_TIME, 0},
    {"03 SUSTAIN", "S", 0, 127, 127, FM_PCT, 0}, {"04 RELEASE", "R", 0, 127, 40, FM_TIME, 0},
    {"01 ATTACK", "A", 0, 127, 0, FM_TIME, 0}, {"02 DECAY", "D", 0, 127, 70, FM_TIME, 0},
    {"03 SUSTAIN", "S", 0, 127, 64, FM_PCT, 0}, {"04 RELEASE", "R", 0, 127, 40, FM_TIME, 0},
    {"01 ATTACK", "A", 0, 127, 0, FM_TIME, 0}, {"02 DECAY", "D", 0, 127, 50, FM_TIME, 0},
    {"03 AMOUNT", "AMT", -64, 63, 0, FM_BIP, 0}, {"04 DEST", "DEST", 0, ND - 1, D_PITCH, FM_DEST, L_DST},
    LFO_PD(1), LFO_PD(2), MX_PD("1"), MX_PD("2"), MX_PD("3"), MX_PD("4"),
    {"EFFECT", "ON", 0, 1, 0, FM_LIST, L_ONOFF}, {"TYPE", "TYPE", 0, 3, 0, FM_LIST, L_DRT},
    {"AMOUNT", "AMT", 0, 127, 40, FM_PCT, 0}, {"MIX", "MIX", 0, 127, 127, FM_PCT, 0},
    {"EFFECT", "ON", 0, 1, 0, FM_LIST, L_ONOFF}, {"BITS", "BITS", 0, 127, 40, FM_PCT, 0},
    {"RATE", "RATE", 0, 127, 20, FM_PCT, 0}, {"MIX", "MIX", 0, 127, 64, FM_PCT, 0},
    {"EFFECT", "ON", 0, 1, 0, FM_LIST, L_ONOFF}, {"RATE", "RATE", 0, 127, 30, FM_PCT, 0},
    {"DEPTH", "DEPTH", 0, 127, 60, FM_PCT, 0}, {"MIX", "MIX", 0, 127, 50, FM_PCT, 0},
    {"EFFECT", "ON", 0, 1, 0, FM_LIST, L_ONOFF}, {"TIME", "TIME", 3, 13, 8, FM_DIV, L_DIV},
    {"FEEDBACK", "FDBK", 0, 127, 60, FM_PCT, 0}, {"TONE", "TONE", 0, 127, 50, FM_PCT, 0},
    {"MIX", "MIX", 0, 127, 30, FM_PCT, 0},
    {"EFFECT", "ON", 0, 1, 0, FM_LIST, L_ONOFF}, {"SIZE", "SIZE", 0, 127, 70, FM_PCT, 0},
    {"DAMP", "DAMP", 0, 127, 60, FM_PCT, 0}, {"MIX", "MIX", 0, 127, 25, FM_PCT, 0},
    {"GLUE", "GLUE", 0, 127, 30, FM_PCT, 0}, {"SUB BOOST", "BOOST", 0, 127, 0, FM_PCT, 0},
    {"OUTPUT", "OUT", 0, 127, 90, FM_VOL, 0},
    {"VOICE MODE", "VOICE", 0, 2, VM_MONO, FM_LIST, L_VM}, {"GLIDE", "GLIDE", 0, 127, 0, FM_TIME, 0},
    {"BEND RANGE", "BEND", 0, 24, 2, FM_SEMI, 0}, {"VELOCITY", "VELO", 0, 127, 40, FM_PCT, 0},
    {"STYLE", "STYLE", 0, NSTYLE - 1, 0, FM_LIST, L_STYLE},
    {"STEP", "ON", 0, 1, 0, FM_LIST, L_ONOFF}, {"PATTERN", "PATTERN", 0, 15, 0, FM_LIST, L_SPAT},
    {"RATE", "RATE", 0, 5, 3, FM_LIST, L_SRATE}, {"GATE", "GATE", 0, 127, 100, FM_PCT, 0},
    {"DEST", "DEST", 0, ND - 1, D_OFF, FM_DEST, L_DST}, {"DEPTH", "DEPTH", -64, 63, 0, FM_BIP, 0},
    {"SMOOTH", "SMOOTH", 0, 127, 10, FM_PCT, 0},
};

/* ---- the factory presets: the defaults, then (parameter, value) pairs, -1 ends */
typedef struct {
    const char *name;
    const int8_t *ov;
} preset_t;
#define PR(nm, ...) {nm, (const int8_t[]){__VA_ARGS__, -1}}
static const preset_t FACTORY[] = {
    PR("DEEP PRESSURE", P_STYLE, 0, A_ENG, E_ANALOG, A_P1, 0, A_LVL, 70, S_LVL, 105, S_WAVE, 0, F_CUT, 70, F_RES, 20,
       F_DRV, 30, AE_R, 30, ME_AMT, 20, ME_D, 35, DR_ON, 1, DR_TYPE, 1, DR_AMT, 30, DR_MIX, 60, MS_SUB, 30, X_DRIFT, 6),
    PR("SUB TIDE", P_STYLE, 0, A_ENG, E_ANALOG, A_P1, 0, A_LVL, 110, F_CUT, 127, F_RES, 0, AE_A, 10, AE_R, 55,
       L1_DEP, 3, L1_DST, D_PITCH, L1_SYNC, 0, L1_RATE, 60, V_GLIDE, 45),
    PR("IRON DUB", P_STYLE, 1, A_ENG, E_ANALOG, A_P1, 110, A_P2, 30, B_ENG, E_ANALOG, B_P1, 85, B_LVL, 60, B_FINE, 8,
       F_CUT, 55, F_RES, 45, F_ENV, 30, FE_D, 60, FE_S, 20, AE_D, 75, AE_S, 90, S_LVL, 80, DL_ON, 1, DL_TIME, 7,
       DL_FB, 85, DL_TONE, 35, DL_MIX, 45, RV_ON, 1, RV_MIX, 25, V_MODE, VM_LEGATO, V_GLIDE, 30),
    PR("NIGHT BUS", P_STYLE, 3, A_ENG, E_WAVE, A_P1, 30, A_P2, 16, B_ENG, E_ANALOG, B_P1, 42, B_LVL, 50, B_OCT, -1,
       F_CUT, 60, F_RES, 50, F_ENV, 40, FE_D, 55, FE_S, 0, AE_D, 60, AE_S, 70, L2_DEP, 20, L2_DST, D_AP1,
       L2_SYNC, 1, L2_DIV, 2, CH_ON, 1, DL_ON, 1, DL_TIME, 8, DL_MIX, 25),
    PR("CONCRETE", P_STYLE, 3, A_ENG, E_FOLD, A_P1, 40, A_P2, 70, A_P3, 30, S_LVL, 60, F_CUT, 80, F_RES, 30,
       AE_D, 45, AE_S, 40, ME_AMT, 30, ME_DST, D_AP1, ME_D, 45, DR_ON, 1, DR_TYPE, 2, DR_AMT, 50, MS_GLUE, 60),
    PR("REESE CLASSIC", P_STYLE, 0, A_ENG, E_SUPER, A_P1, 40, A_P2, 80, A_P3, 0, A_LVL, 100, S_LVL, 70, F_CUT, 75,
       F_RES, 25, L2_DEP, 18, L2_DST, D_AP1, L2_SYNC, 0, L2_RATE, 30, L2_SHP, 1, CH_ON, 1, CH_MIX, 40, X_DRIFT, 30),
    PR("WIDE REESE", P_STYLE, 8, A_ENG, E_SUPER, A_P1, 70, A_P2, 100, A_P3, 127, B_ENG, E_SUPER, B_P1, 50, B_P3, 0,
       B_LVL, 70, B_OCT, -1, S_LVL, 85, F_CUT, 68, F_RES, 35, F_DRV, 40, CH_ON, 1, CH_DEP, 80, CH_MIX, 60,
       DR_ON, 1, DR_AMT, 25, DR_MIX, 50),
    PR("WOBBLE 01", P_STYLE, 2, A_ENG, E_ANALOG, A_P1, 85, B_ENG, E_ANALOG, B_P1, 127, B_LVL, 70, B_FINE, -10,
       S_LVL, 90, F_CUT, 40, F_RES, 70, F_DRV, 50, L1_SHP, 0, L1_DIV, 9, L1_DEP, 45, L1_DST, D_CUT, L1_TRIG, 1,
       DR_ON, 1, DR_AMT, 50, DR_MIX, 80, M1_SRC, SR_WHEEL, M1_DST, D_WOB, M1_AMT, 40),
    PR("YOI TALKER", P_STYLE, 2, A_ENG, E_VOCAL, A_P1, 10, A_P2, 64, A_P3, 40, B_ENG, E_ANALOG, B_P1, 85, B_LVL, 60,
       S_LVL, 80, F_TYPE, FT_VOWEL, F_CUT, 20, F_RES, 80, L1_SHP, 1, L1_DIV, 9, L1_DEP, 50, L1_DST, D_CUT,
       L1_TRIG, 1, L2_SHP, 1, L2_DIV, 9, L2_DEP, 40, L2_DST, D_AP1, L2_TRIG, 1, DR_ON, 1, DR_AMT, 45),
    PR("NEURO GROWL", P_STYLE, 8, A_ENG, E_FM, A_P1, 60, A_P2, 48, A_P3, 50, B_ENG, E_FOLD, B_P1, 50, B_LVL, 60,
       X_MODE, 2, X_AMT, 30, S_LVL, 80, F_TYPE, FT_COMB, F_CUT, 70, F_RES, 75, F_KEY, 127, L1_SHP, 6, L1_DIV, 11,
       L1_DEP, 30, L1_DST, D_AP1, L1_TRIG, 1, M1_SRC, SR_LFO1, M1_DST, D_CUT, M1_AMT, 25, DR_ON, 1, DR_TYPE, 3,
       DR_AMT, 40, MS_GLUE, 70),
    PR("METAL FM", P_STYLE, 2, A_ENG, E_FM, A_P1, 80, A_P2, 120, A_P3, 30, S_LVL, 90, F_CUT, 85, F_RES, 40,
       L1_SHP, 4, L1_DIV, 10, L1_DEP, 35, L1_DST, D_AP1, L1_TRIG, 1, L1_SMO, 40, DR_ON, 1, DR_TYPE, 2, DR_AMT, 35),
    PR("808 DROP", P_STYLE, 6, A_ENG, E_ANALOG, A_P1, 0, A_P3, 60, A_LVL, 120, F_CUT, 127, F_RES, 0, AE_D, 85,
       AE_S, 0, AE_R, 60, ME_D, 40, ME_AMT, 28, ME_DST, D_PITCH, V_GLIDE, 35, DR_ON, 1, DR_TYPE, 1, DR_AMT, 35),
    PR("DUB STAB", P_STYLE, 9, A_ENG, E_ANALOG, A_P1, 85, B_ENG, E_ANALOG, B_P1, 85, B_LVL, 90, B_SEMI, 7,
       F_TYPE, FT_LP12, F_CUT, 50, F_RES, 40, F_ENV, 45, FE_D, 50, FE_S, 0, AE_D, 55, AE_S, 0, AE_R, 55,
       V_MODE, VM_POLY, DL_ON, 1, DL_TIME, 7, DL_FB, 90, DL_TONE, 30, DL_MIX, 55, RV_ON, 1, RV_SIZE, 100, RV_MIX, 40),
    PR("TECHNO RUMBLE", P_STYLE, 3, A_ENG, E_ANALOG, A_P1, 50, S_LVL, 100, N_LVL, 25, N_COL, 90, F_CUT, 45,
       F_RES, 55, F_ENV, 30, FE_D, 45, FE_S, 10, AE_D, 60, AE_S, 50, RV_ON, 1, RV_SIZE, 120, RV_DAMP, 100,
       RV_MIX, 50, DR_ON, 1, DR_AMT, 60, DR_MIX, 70, MS_GLUE, 50),
    PR("ACID LINE", P_STYLE, 3, A_ENG, E_ANALOG, A_P1, 85, S_LVL, 40, F_TYPE, FT_LP24, F_CUT, 35, F_RES, 100,
       F_ENV, 50, FE_D, 45, FE_S, 0, AE_D, 50, AE_S, 80, V_MODE, VM_LEGATO, V_GLIDE, 25, DR_ON, 1, DR_TYPE, 1,
       DR_AMT, 50, DL_ON, 1, DL_TIME, 10, DL_MIX, 20),
    PR("PHASE HORN", P_STYLE, 2, A_ENG, E_PHASE, A_P1, 70, A_P2, 75, A_P3, 30, S_LVL, 80, F_CUT, 95, F_RES, 20,
       L1_DIV, 10, L1_DEP, 40, L1_DST, D_AP1, L1_TRIG, 1, DR_ON, 1, DR_AMT, 40),
    PR("SYNC RIPPER", P_STYLE, 8, A_ENG, E_SYNC, A_P1, 40, A_P2, 20, A_P3, 30, S_LVL, 80, F_CUT, 100, F_RES, 25,
       ME_AMT, 40, ME_DST, D_AP1, ME_D, 60, L1_DIV, 11, L1_DEP, 30, L1_DST, D_AP1, DR_ON, 1, DR_TYPE, 2, DR_AMT, 45),
    PR("HOLLOW TUBE", P_STYLE, 2, A_ENG, E_WAVE, A_P1, 70, A_P2, 32, A_P3, 30, S_LVL, 90, F_TYPE, FT_NOTCH, F_CUT, 50,
       F_RES, 60, L1_SHP, 0, L1_DIV, 10, L1_DEP, 40, L1_DST, D_CUT, L1_TRIG, 1),
    PR("WUB MACHINE", P_STYLE, 2, A_ENG, E_SUPER, A_P1, 30, A_P3, 64, B_ENG, E_FM, B_P1, 50, B_P2, 16, B_LVL, 60,
       S_LVL, 90, F_CUT, 30, F_RES, 60, F_DRV, 70, L1_SHP, 6, L1_DIV, 10, L1_DEP, 55, L1_DST, D_CUT, L1_TRIG, 1,
       M1_SRC, SR_LFO1, M1_DST, D_BP1, M1_AMT, 30, DR_ON, 1, DR_AMT, 60, MS_GLUE, 70),
    PR("DIGI BITE", P_STYLE, 10, A_ENG, E_WAVE, A_P1, 60, A_P2, 96, A_P3, 20, S_LVL, 85, F_CUT, 90, F_RES, 30,
       CR_ON, 1, CR_BITS, 70, CR_RATE, 40, CR_MIX, 70, L1_DIV, 11, L1_DEP, 45, L1_DST, D_AP1, L1_TRIG, 1),
    PR("ROLLER SUB", P_STYLE, 0, A_ENG, E_ANALOG, A_P1, 42, A_LVL, 60, S_LVL, 110, S_WAVE, 1, F_CUT, 60, F_RES, 10,
       AE_D, 50, AE_S, 100, AE_R, 25, V_MODE, VM_LEGATO, V_GLIDE, 20, MS_SUB, 50),
    PR("HOOVER DNB", P_STYLE, 4, A_ENG, E_SUPER, A_P1, 90, A_P2, 110, A_P3, 127, B_ENG, E_ANALOG, B_P1, 127, B_P2, 80,
       B_LVL, 60, B_OCT, -1, S_LVL, 60, F_CUT, 95, ME_AMT, -20, ME_DST, D_PITCH, ME_A, 40, ME_D, 70,
       V_GLIDE, 40, CH_ON, 1, CH_MIX, 60, L2_DEP, 25, L2_DST, D_BP2, L2_SYNC, 0, L2_RATE, 50),
    PR("STEPPA DUB", P_STYLE, 1, A_ENG, E_ANALOG, A_P1, 10, A_P3, 30, S_LVL, 110, F_CUT, 45, F_RES, 15, AE_D, 70,
       AE_S, 60, DL_ON, 1, DL_TIME, 6, DL_FB, 70, DL_TONE, 25, DL_MIX, 35, MS_SUB, 60, MS_GLUE, 40),
    PR("LIQUID SUB", P_STYLE, 0, A_ENG, E_ANALOG, A_P1, 42, A_LVL, 60, S_LVL, 110, F_CUT, 75, AE_A, 8, AE_R, 50,
       V_MODE, VM_LEGATO, V_GLIDE, 30, CH_ON, 1, CH_MIX, 30),
    PR("ROLLING REESE", P_STYLE, 0, A_ENG, E_SUPER, A_P1, 55, A_P3, 0, B_ENG, E_ANALOG, B_P1, 85, B_OCT, -1, B_LVL, 50,
       S_LVL, 90, F_CUT, 70, F_RES, 30, L2_DEP, 25, L2_DST, D_AP1, L2_SYNC, 1, L2_DIV, 6, DR_ON, 1, DR_TYPE, 1, DR_AMT, 30),
    PR("AMEN SUB", P_STYLE, 4, A_ENG, E_ANALOG, A_P1, 0, A_P3, 50, S_LVL, 100, ME_AMT, 20, ME_DST, D_PITCH, ME_D, 30,
       AE_D, 70, AE_S, 60, V_GLIDE, 20),
    PR("JUNGLE BOOM", P_STYLE, 4, A_ENG, E_ANALOG, A_P1, 0, A_LVL, 115, AE_D, 95, AE_S, 0, AE_R, 70, ME_AMT, 24,
       ME_DST, D_PITCH, ME_D, 45, DR_ON, 1, DR_TYPE, 1, DR_AMT, 45),
    PR("RIDDIM SQUARE", P_STYLE, 2, A_ENG, E_ANALOG, A_P1, 127, A_P2, 40, B_ENG, E_ANALOG, B_P1, 127, B_OCT, -1, B_LVL, 60,
       S_LVL, 80, F_CUT, 35, F_RES, 50, L1_SHP, 4, L1_DIV, 10, L1_DEP, 50, L1_DST, D_CUT, L1_TRIG, 1, DR_ON, 1,
       DR_TYPE, 2, DR_AMT, 55),
    PR("TEAROUT", P_STYLE, 2, A_ENG, E_FM, A_P1, 90, A_P2, 64, A_P3, 70, B_ENG, E_FOLD, B_P1, 60, B_LVL, 50, X_MODE, 2,
       X_AMT, 50, S_LVL, 80, F_TYPE, FT_COMB, F_CUT, 80, F_RES, 80, F_KEY, 127, L1_SHP, 6, L1_DIV, 11, L1_DEP, 40,
       L1_DST, D_AP1, L1_TRIG, 1, DR_ON, 1, DR_TYPE, 3, DR_AMT, 50, MS_GLUE, 90),
    PR("BROSTEP SAW", P_STYLE, 2, A_ENG, E_SUPER, A_P1, 60, A_P3, 127, S_LVL, 85, F_CUT, 30, F_RES, 70, F_DRV, 60,
       L1_DIV, 9, L1_DEP, 55, L1_DST, D_CUT, L1_TRIG, 1, DR_ON, 1, DR_AMT, 60),
    PR("NEURO STAB", P_STYLE, 8, A_ENG, E_SYNC, A_P1, 70, A_P2, 40, B_ENG, E_FM, B_P1, 50, B_LVL, 60, X_MODE, 1,
       S_LVL, 70, F_CUT, 85, F_RES, 45, ME_AMT, 50, ME_DST, D_AP1, ME_D, 40, AE_D, 50, AE_S, 30, DR_ON, 1, DR_TYPE, 2,
       DR_AMT, 50),
    PR("SPEED BASS", P_STYLE, 5, A_ENG, E_ANALOG, A_P1, 100, S_LVL, 60, F_TYPE, FT_LP12, F_CUT, 50, F_RES, 50, F_ENV, 45,
       FE_D, 40, FE_S, 10, AE_D, 55, AE_S, 70, V_MODE, VM_LEGATO, V_GLIDE, 22),
    PR("UKG ORGAN", P_STYLE, 5, MS_VOL, 120, A_LVL, 120, A_ENG, E_WAVE, A_P1, 40, A_P2, 16, B_ENG, E_ANALOG, B_P1, 0, B_OCT, 1, B_LVL, 50,
       AE_D, 60, AE_S, 50, F_CUT, 90, CH_ON, 1),
    PR("TRAP 808", P_STYLE, 6, A_ENG, E_ANALOG, A_P1, 0, A_P3, 70, A_LVL, 120, F_CUT, 127, F_RES, 0, AE_D, 100, AE_S, 40,
       AE_R, 75, V_GLIDE, 50, DR_ON, 1, DR_TYPE, 1, DR_AMT, 45, ME_AMT, 18, ME_DST, D_PITCH, ME_D, 30),
    PR("DEEP HOUSE", P_STYLE, 7, MS_VOL, 115, A_ENG, E_ANALOG, A_P1, 42, S_LVL, 70, F_TYPE, FT_LP12, F_CUT, 60, F_ENV, 25,
       FE_D, 45, AE_D, 60, AE_S, 60, AE_R, 30),
    PR("HOUSE ORGAN", P_STYLE, 7, MS_VOL, 120, A_LVL, 120, A_ENG, E_FM, A_P1, 30, A_P2, 28, AE_D, 45, AE_S, 30, V_MODE, VM_POLY, F_CUT, 95,
       S_LVL, 40),
    PR("BERLIN RUMBLE", P_STYLE, 3, MS_VOL, 120, A_LVL, 120, A_ENG, E_ANALOG, A_P1, 85, N_LVL, 30, N_COL, 100, S_LVL, 90, F_CUT, 40, F_RES, 40,
       AE_D, 72, AE_S, 0, RV_ON, 1, RV_SIZE, 120, RV_DAMP, 110, RV_MIX, 60, DL_ON, 1, DL_TIME, 9, DL_FB, 70,
       DL_TONE, 25, DL_MIX, 30, MS_GLUE, 60),
    PR("DUBTECH PULSE", P_STYLE, 9, MS_VOL, 120, A_LVL, 120, A_ENG, E_ANALOG, A_P1, 85, B_ENG, E_ANALOG, B_P1, 85, B_LVL, 80, B_SEMI, 7,
       F_TYPE, FT_LP12, F_CUT, 40, F_RES, 45, F_ENV, 40, FE_D, 55, FE_S, 0, AE_D, 60, AE_S, 0, V_MODE, VM_POLY,
       DL_ON, 1, DL_TIME, 8, DL_FB, 95, DL_TONE, 20, DL_MIX, 60, RV_ON, 1, RV_SIZE, 120, RV_MIX, 50, CH_ON, 1),
    PR("SKANK UNIT", P_STYLE, 1, A_ENG, E_ANALOG, A_P1, 42, S_LVL, 110, F_CUT, 50, AE_A, 10, AE_D, 75, AE_S, 70,
       DL_ON, 1, DL_TIME, 6, DL_FB, 60, DL_MIX, 25, MS_SUB, 70),
    PR("BREAKS HOOVER", P_STYLE, 10, A_ENG, E_SUPER, A_P1, 100, A_P2, 120, A_P3, 127, ME_AMT, -25, ME_DST, D_PITCH,
       ME_A, 30, ME_D, 60, F_CUT, 90, S_LVL, 60, V_GLIDE, 35, CH_ON, 1, CH_MIX, 70),
    PR("MINIMAL CLICK", P_STYLE, 11, MS_VOL, 120, A_LVL, 120, A_ENG, E_PHASE, A_P1, 60, A_P2, 0, S_LVL, 70, AE_D, 35, AE_S, 0, AE_R, 20, F_CUT, 70,
       F_ENV, 30, FE_D, 25, FE_S, 0, DL_ON, 1, DL_TIME, 10, DL_MIX, 20),
    PR("YOI RIDDIM", P_STYLE, 2, A_ENG, E_VOCAL, A_P1, 64, A_P2, 50, S_LVL, 80, F_TYPE, FT_VOWEL, F_CUT, 40, F_RES, 90,
       L1_SHP, 7, L1_DIV, 10, L1_DEP, 60, L1_DST, D_CUT, L1_TRIG, 1, DR_ON, 1, DR_AMT, 45),
    PR("INIT", P_STYLE, 0),
};
#define NFACTORY (sizeof FACTORY / sizeof FACTORY[0])

static void patch_defaults(int8_t *p)
{
    uint32_t i;
    for (i = 0; i < NP_MAX; i++)
        p[i] = i < NP ? PD[i].def : 0;
    p[B_LVL] = 0;
    p[B_ENG] = E_SUPER;
    p[L2_SYNC] = 0;
    p[L2_SHP] = 1;
}

static void patch_sanitize(int8_t *p)
{
    uint32_t i;
    for (i = 0; i < NP; i++)
        p[i] = (int8_t)clampi(p[i], PD[i].lo, PD[i].hi);
}

static void preset_factory(uint32_t k, int8_t *p, char *name)
{
    const int8_t *o;
    uint32_t i;
    if (k >= NFACTORY) k = 0;
    patch_defaults(p);
    for (o = FACTORY[k].ov; o[0] >= 0; o += 2)
        p[o[0]] = o[1];
    patch_sanitize(p);
    for (i = 0; i < 13u && FACTORY[k].name[i]; i++)
        name[i] = FACTORY[k].name[i];
    name[i] = 0;
}
