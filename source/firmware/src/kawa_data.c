/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: names, parameter descriptions, factory sounds, themes, chords, scales, the live effects and the
 * magic tools. Text on the screen is upper case without accents (the font is ASCII), in three languages:
 * French, English, Spanish (OPTIONS, KNOB 2). Tables hold [3] names in that order. */

enum { LANG_FR, LANG_EN, LANG_ES, LANG_N };
static uint8_t lang = LANG_EN;                  /* the screen's language (the settings sector) */
#define TR(fr, en, es) (lang == LANG_EN ? (en) : lang == LANG_ES ? (es) : (fr))
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static const char *const LANG_CODE[LANG_N] = {"FR", "EN", "ES"};

/* ---- engines: each of the eight tracks plays one, picked with its sound (SOUND, PRESETS) */
#define NUSR 4u                                /* the user's sample slots (kawa_core.c) */
#define NTRK 6u                                /* six tracks: the FM-1's audio interrupt keeps a wide margin */
enum { E_PULSE, E_WAVE, E_NOISE, E_FM, E_KIT, E_SAMPLE, E_N };
static const char *const ENGINE_NAMES[LANG_N][E_N] = {{"CARRE", "ONDE", "BRUIT", "FM", "KIT", "SAMPLE"},
                                                      {"PULSE", "WAVE", "NOISE", "FM", "KIT", "SAMPLE"},
                                                      {"PULSO", "ONDA", "RUIDO", "FM", "KIT", "SAMPLE"}};
#define ENGINE_NAME ENGINE_NAMES[lang]
static const uint8_t ENGINE_ICON[E_N] = {ICO_PULSE, ICO_WAVE, ICO_NOISE, ICO_FM, ICO_KIT, ICO_SAMPLE};

/* ---- themes (OPTIONS, KNOB 1): the screen's colours. KAWA is the look of 0.1 */
typedef struct {
    const char *name;
    uint16_t bg, bg2, beat, rule, faint, mid, txt, gold, rec, go, ink;   /* ink: text on a lit tile */
    uint16_t trk[NTRK];
} theme_t;
static const theme_t THEMES[3] = {
    {"KAWA", RGB(18, 12, 36), RGB(26, 18, 50), RGB(38, 28, 72), RGB(56, 44, 98), RGB(118, 106, 170), RGB(176, 166, 224),
     RGB(240, 236, 255), RGB(255, 222, 110), RGB(255, 64, 96), RGB(96, 255, 150), RGB(18, 12, 36),
     {RGB(255, 95, 180), RGB(255, 176, 58), RGB(62, 216, 255), RGB(232, 240, 96), RGB(98, 242, 154), RGB(169, 139, 255)}},
    {"GAME BOY", RGB(15, 56, 15), RGB(24, 70, 24), RGB(40, 92, 36), RGB(56, 108, 48), RGB(110, 150, 70), RGB(160, 196, 60),
     RGB(214, 236, 150), RGB(236, 250, 170), RGB(250, 120, 90), RGB(200, 248, 120), RGB(15, 56, 15),
     {RGB(200, 240, 110), RGB(170, 220, 90), RGB(220, 248, 150), RGB(150, 200, 70), RGB(230, 250, 170), RGB(180, 230, 100)}},
    {"BONBON", RGB(255, 244, 250), RGB(250, 230, 244), RGB(240, 210, 234), RGB(222, 190, 218), RGB(150, 120, 160),
     RGB(100, 72, 112), RGB(50, 28, 62), RGB(200, 106, 10), RGB(226, 30, 84), RGB(20, 150, 80), RGB(255, 255, 255),
     {RGB(230, 50, 140), RGB(226, 120, 10), RGB(10, 140, 200), RGB(120, 150, 10), RGB(20, 160, 100), RGB(130, 70, 220)}},
};
#define NTHEME 3u
static uint8_t theme;
static uint16_t C_BG, C_BG2, C_BEAT, C_RULE, C_FAINT, C_MID, C_TXT, C_GOLD, C_REC, C_GO, C_INK;
static uint16_t TRK_COL[NTRK];
static void theme_set(uint32_t k)
{
    const theme_t *t = &THEMES[k % NTHEME];
    uint32_t i;
    theme = (uint8_t)(k % NTHEME);
    C_BG = t->bg; C_BG2 = t->bg2; C_BEAT = t->beat; C_RULE = t->rule; C_FAINT = t->faint; C_MID = t->mid;
    C_TXT = t->txt; C_GOLD = t->gold; C_REC = t->rec; C_GO = t->go; C_INK = t->ink;
    for (i = 0; i < NTRK; i++)
        TRK_COL[i] = t->trk[i];
}
/* one knob, one colour: KNOB 1..4 everywhere (the cards, the bottom line, the emulator's rings) */
static const uint16_t KNOB_COL[4] = {RGB(255, 92, 92), RGB(255, 204, 64), RGB(92, 226, 116), RGB(84, 164, 255)};
/* one page, one colour (the page's pill, the emulator's buttons) */
enum { PG_TRACK, PG_GRID, PG_SOUND, PG_ENV, PG_MIX, PG_MAGIC, PG_SCALE, PG_SONG, PG_PROJ, PG_OPT, PG_FX, PG_HELP, PG_BOOT,
       PG_GROOVE, PG_N };                       /* (GROOVE last: the emulator knows the others by their number) */
static const uint16_t PAGE_COL[PG_N] = {RGB(169, 139, 255), RGB(255, 96, 146), RGB(62, 216, 255), RGB(98, 242, 154),
                                        RGB(255, 160, 60), RGB(255, 210, 90), RGB(200, 240, 90), RGB(255, 160, 60),
                                        RGB(100, 150, 255), RGB(255, 160, 60), RGB(255, 120, 200), RGB(169, 139, 255),
                                        RGB(169, 139, 255), RGB(255, 160, 60)};

/* ---- the 24 parameters of a track: SOUND 1, SOUND 2 (the engine's), ENVELOPE, MIX, EFFECT, LFO */
#define NPARAM 28u
enum { P_S0, P_S1, P_S2, P_S3, P_S4, P_S5, P_S6, P_S7, P_ATT, P_DEC, P_SUS, P_REL, P_VOL, P_PAN, P_ECHO, P_VERB,
       P_FXT, P_FXA, P_FXB, P_FXM, P_LFOD, P_LFOR, P_LFOA, P_LFOW, P_MENVA, P_MVELA, P_MENVP, P_MLFRT };
/* SOUND for PULSE / WAVE / NOISE */
enum { P_SHAPE = 0, P_SWEEP, P_VIB, P_TON, P_CRUSH, P_DETUNE, P_ARPV, P_GLIDE };
/* FM */
enum { P_FMPAT = 0, P_FMALG, P_FMRAT, P_FMFB, P_FMBRI, P_FMMEG, P_FMDET, P_FMVIB };
/* KIT */
enum { P_KIT = 0, P_TUNE, P_DECAY, P_DRIVE, P_KCRUSH, P_KTON };
/* SAMPLE */
enum { P_SRC = 0, P_SMODE, P_STUNE, P_FIT, P_LEN, P_SDRIVE, P_SCRUSH, P_STON };
enum { K_NUM, K_SIGN, K_LIST, K_NAMES, K_PAN, K_MS, K_SRC };   /* K_LIST: n values x 3 languages, flat */
typedef struct {
    const char *name[LANG_N];
    int8_t lo, hi;
    uint8_t kind;
    const char *const *list;
} pdesc_t;
static const char *const L_DUTY[] = {"12%", "25%", "50%", "75%", "PWM", "12%", "25%", "50%", "75%", "PWM",
                                     "12%", "25%", "50%", "75%", "PWM"};
static const char *const L_WAVE[] = {"TRI", "SCIE", "SINUS", "DOUX", "GB", "ORGUE", "VOIX", "FM",
                                     "TRI", "SAW", "SINE", "SOFT", "GB", "ORGAN", "VOICE", "FM",
                                     "TRI", "SIERRA", "SENO", "SUAVE", "GB", "ORGANO", "VOZ", "FM"};
static const char *const L_NOISE[] = {"LONG", "METAL", "LONG", "METAL", "LARGO", "METAL"};
static const char *const L_DUR[] = {"1/4", "1/2", "3/4", "1", "1.5", "2", "3", "4", "6", "8", "12", "TOUT",
                                    "1/4", "1/2", "3/4", "1", "1.5", "2", "3", "4", "6", "8", "12", "ALL",
                                    "1/4", "1/2", "3/4", "1", "1.5", "2", "3", "4", "6", "8", "12", "TODO"};
static const uint8_t DUR_Q[12] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 0};   /* quarter slices, 0 = to the end */
static const char *const L_ONOFF[] = {"NON", "OUI", "NO", "YES", "NO", "SI"};
static const char *const L_SMODE[] = {"TRANCHE", "NOTES", "SLICES", "NOTES", "CORTES", "NOTAS"};
static const char *const L_ALG[] = {"SON", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
                                    "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32",
                                    "SOUND", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
                                    "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32",
                                    "SONIDO", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
                                    "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32"};
/* the effect of a track or of the master (EFFECT; MIX on MASTER) */
enum { FXT_OFF, FXT_LPF, FXT_HPF, FXT_BPF, FXT_DIST, FXT_CRUSH, FXT_DECIM, FXT_RING, FXT_TREM, FXT_WAH, FXT_N };
static const char *const L_FXT[] = {"AUCUN", "FILT BAS", "FILT HAU", "PASS BND", "DISTO", "ECRASE", "DECIME", "ANNEAU", "TREMOLO", "WAH",
                                    "NONE", "LOWPASS", "HIPASS", "BANDPASS", "DRIVE", "CRUSH", "LO-FI", "RING", "TREMOLO", "WAH",
                                    "NADA", "FILT BAJ", "FILT ALT", "PASA BND", "DISTOR", "TRITURA", "DIEZMA", "ANILLO", "TREMOLO", "WAH"};
static const uint8_t FXT_ICON[FXT_N] = {ICO_ERASE, ICO_LPF, ICO_HPF, ICO_BPF, ICO_DIST, ICO_CRUSH, ICO_PIXEL, ICO_RING, ICO_TREM, ICO_WOB};
/* what the effect's A and B do, per type */
static const char *const FXA_NAMES[LANG_N][FXT_N] = {
    {"A", "COUPURE", "COUPURE", "CENTRE", "GAIN", "BITS", "TAUX", "FREQ", "VITESSE", "VITESSE"},
    {"A", "CUTOFF", "CUTOFF", "CENTER", "GAIN", "BITS", "RATE", "FREQ", "RATE", "RATE"},
    {"A", "CORTE", "CORTE", "CENTRO", "GANANC", "BITS", "TASA", "FREC", "VELOC", "VELOC"}};
static const char *const FXB_NAMES[LANG_N][FXT_N] = {
    {"B", "RESO", "RESO", "RESO", "TON", "-", "-", "-", "PROFOND", "PROFOND"},
    {"B", "RESO", "RESO", "RESO", "TONE", "-", "-", "-", "DEPTH", "DEPTH"},
    {"B", "RESO", "RESO", "RESO", "TONO", "-", "-", "-", "PROFUND", "PROFUND"}};
/* the LFO */
enum { LFD_OFF, LFD_PITCH, LFD_VOL, LFD_FILT, LFD_PAN, LFD_SHAPE, LFD_N };
static const char *const L_LFOD[] = {"AUCUN", "HAUTEUR", "VOLUME", "EFFET A", "PANO", "FORME",
                                     "NONE", "PITCH", "VOLUME", "EFFECT A", "PAN", "SHAPE",
                                     "NADA", "ALTURA", "VOLUMEN", "EFECTO A", "PANEO", "FORMA"};
static const char *const L_LFOR[] = {"4 MES", "2 MES", "1 MES", "1/2", "1/4", "1/8", "1/16", "1/32", "LENT", "MOYEN", "VITE",
                                     "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/32", "SLOW", "MEDIUM", "FAST",
                                     "4 COMP", "2 COMP", "1 COMP", "1/2", "1/4", "1/8", "1/16", "1/32", "LENTO", "MEDIO", "RAPIDO"};
static const char *const L_LFOW[] = {"SINUS", "TRIANGLE", "CARRE", "SCIE", "HASARD", "SINE", "TRIANGLE", "SQUARE", "SAW", "RANDOM",
                                     "SENO", "TRIANG", "CUADRADA", "SIERRA", "AZAR"};

#define PDN(fr, en, es) {fr, en, es}
#define PD_NONE {{0, 0, 0}, 0, 0, 0, 0}
#define PD_ENV {PDN("ATTAQUE", "ATTACK", "ATAQUE"), 0, 15, K_MS, 0}, {PDN("DECLIN", "DECAY", "CAIDA"), 0, 15, K_MS, 0}, \
               {PDN("TENUE", "SUSTAIN", "SOSTEN"), 0, 15, K_NUM, 0}, {PDN("RELACHE", "RELEASE", "SUELTA"), 0, 15, K_MS, 0}
#define PD_MIX {PDN("VOLUME", "VOLUME", "VOLUMEN"), 0, 15, K_NUM, 0}, {PDN("PANO", "PAN", "PANEO"), -7, 7, K_PAN, 0}, \
               {PDN("ECHO", "ECHO", "ECO"), 0, 15, K_NUM, 0}, {PDN("REVERB", "REVERB", "REVERB"), 0, 15, K_NUM, 0}
#define PD_FX {PDN("EFFET", "EFFECT", "EFECTO"), 0, FXT_N - 1, K_LIST, L_FXT}, {PDN("A", "A", "A"), 0, 15, K_NUM, 0}, \
              {PDN("B", "B", "B"), 0, 15, K_NUM, 0}, {PDN("DOSAGE", "MIX", "MEZCLA"), 0, 15, K_NUM, 0}
#define PD_LFO {PDN("CIBLE", "TARGET", "DESTINO"), 0, LFD_N - 1, K_LIST, L_LFOD}, {PDN("VITESSE", "RATE", "VELOC"), 0, 10, K_LIST, L_LFOR}, \
               {PDN("PROFOND", "DEPTH", "PROFUND"), 0, 15, K_NUM, 0}, {PDN("ONDE", "WAVE", "ONDA"), 0, 4, K_LIST, L_LFOW}
#define PD_MOD {PDN("ENV>EFF", "ENV>FX", "ENV>EFE"), -15, 15, K_SIGN, 0}, {PDN("VEL>EFF", "VEL>FX", "VEL>EFE"), 0, 15, K_NUM, 0}, \
               {PDN("ENV>HAUT", "ENV>PIT", "ENV>ALT"), -12, 12, K_SIGN, 0}, {PDN("LFO NOTE", "LFO KEY", "LFO NOTA"), 0, 1, K_LIST, L_ONOFF}
#define PD_TONAL_REST {PDN("BALAYE", "SWEEP", "BARRIDO"), -15, 15, K_SIGN, 0}, {PDN("VIBRATO", "VIBRATO", "VIBRATO"), 0, 15, K_NUM, 0}, \
                      {PDN("TON", "TONE", "TONO"), 0, 15, K_NUM, 0}, {PDN("ECRASE", "CRUSH", "TRITURA"), 0, 7, K_NUM, 0}, \
                      {PDN("DESACC", "DETUNE", "DESAFIN"), 0, 15, K_NUM, 0}, {PDN("ARP VIT", "ARP SPD", "ARP VEL"), 1, 8, K_NUM, 0}, \
                      {PDN("GLISSE", "GLIDE", "GLISA"), 0, 15, K_NUM, 0}
#define PD_SHAPE PDN("FORME", "SHAPE", "FORMA")
static const pdesc_t PDESC[E_N][NPARAM] = {
    {{PD_SHAPE, 0, 4, K_LIST, L_DUTY}, PD_TONAL_REST, PD_ENV, PD_MIX, PD_FX, PD_LFO, PD_MOD},
    {{PD_SHAPE, 0, 7, K_LIST, L_WAVE}, PD_TONAL_REST, PD_ENV, PD_MIX, PD_FX, PD_LFO, PD_MOD},
    {{PD_SHAPE, 0, 1, K_LIST, L_NOISE}, PD_TONAL_REST, PD_ENV, PD_MIX, PD_FX, PD_LFO, PD_MOD},
    {{PDN("SON FM", "PATCH", "SONIDO"), 0, FM_NPATCH - 1, K_NAMES, &FM_PATCH_NAME[0][0]},
     {PDN("ALGO", "ALGO", "ALGO"), 0, 32, K_LIST, L_ALG}, {PDN("RATIO", "RATIO", "RATIO"), -8, 8, K_SIGN, 0},
     {PDN("RETOUR", "FEEDBK", "RETORNO"), -7, 7, K_SIGN, 0}, {PDN("BRILLE", "BRIGHT", "BRILLO"), -15, 15, K_SIGN, 0},
     {PDN("MOD ENV", "MOD ENV", "MOD ENV"), -8, 8, K_SIGN, 0}, {PDN("DESACC", "DETUNE", "DESAFIN"), 0, 15, K_NUM, 0},
     {PDN("VIBRATO", "VIBRATO", "VIBRATO"), 0, 15, K_NUM, 0}, PD_ENV, PD_MIX, PD_FX, PD_LFO, PD_MOD},
    {{PDN("KIT", "KIT", "KIT"), 0, NKIT - 1, K_LIST, L_KITS}, {PDN("ACCORD", "TUNE", "AFINA"), -12, 12, K_SIGN, 0},
     {PDN("DECLIN", "DECAY", "CAIDA"), -8, 8, K_SIGN, 0}, {PDN("SATURE", "DRIVE", "SATURA"), 0, 15, K_NUM, 0},
     {PDN("ECRASE", "CRUSH", "TRITURA"), 0, 7, K_NUM, 0}, {PDN("TON", "TONE", "TONO"), 0, 15, K_NUM, 0}, PD_NONE, PD_NONE,
     PD_ENV, PD_MIX, PD_FX, PD_LFO, PD_MOD},
    {{PDN("SOURCE", "SOURCE", "FUENTE"), 0, (int8_t)(BRK_N + NUSR - 1u), K_SRC, 0}, {PDN("MODE", "MODE", "MODO"), 0, 1, K_LIST, L_SMODE},
     {PDN("ACCORD", "TUNE", "AFINA"), -12, 12, K_SIGN, 0}, {PDN("CALE", "FIT", "AJUSTE"), 0, 1, K_LIST, L_ONOFF},
     {PDN("DUREE", "LENGTH", "LARGO"), 0, 11, K_LIST, L_DUR}, {PDN("SATURE", "DRIVE", "SATURA"), 0, 15, K_NUM, 0},
     {PDN("ECRASE", "CRUSH", "TRITURA"), 0, 7, K_NUM, 0}, {PDN("TON", "TONE", "TONO"), 0, 15, K_NUM, 0},
     PD_ENV, PD_MIX, PD_FX, PD_LFO, PD_MOD},
};
/* the pages of the SOUND page (EDIT again: the next one); the FM track has two more: its operators; a SAMPLE
 * track one: its sample's edit. Every track has its TABLE last */
enum { SP_SON1, SP_SON2, SP_OP, SP_OPENV, SP_SMP, SP_FX, SP_LFO, SP_MOD, SP_TABLE, SP_N };
static const uint8_t SP_FIRST[SP_N] = {P_S0, P_S4, 0, 0, 0, P_FXT, P_LFOD, P_MENVA, 0};
static const char *const SP_NAMES[LANG_N][SP_N] = {
    {"SON", "SON 2", "OPERATEUR", "ENV OPERAT", "SAMPLE", "EFFET", "LFO", "MODULATION", "TABLE"},
    {"SOUND", "SOUND 2", "OPERATOR", "OP ENVELOPE", "SAMPLE EDIT", "EFFECT", "LFO", "MODULATION", "TABLE"},
    {"SONIDO", "SONIDO 2", "OPERADOR", "ENV OPERAD", "EDITAR SAMPLE", "EFECTO", "LFO", "MODULACION", "TABLA"}};
static const uint8_t SP_LIST_FM[8] = {SP_SON1, SP_SON2, SP_OP, SP_OPENV, SP_FX, SP_LFO, SP_MOD, SP_TABLE};
static const uint8_t SP_LIST_SMP[7] = {SP_SON1, SP_SON2, SP_SMP, SP_FX, SP_LFO, SP_MOD, SP_TABLE};
static const uint8_t SP_LIST[6] = {SP_SON1, SP_SON2, SP_FX, SP_LFO, SP_MOD, SP_TABLE};
/* an FM operator's knobs (OPERATOR, OP ENV): what they change in the patch is in kawa_edit.c (op_get, op_set) */
enum { OPK_LEVEL, OPK_RATIO, OPK_FINE, OPK_DET, OPK_ATT, OPK_DEC, OPK_SUS, OPK_REL, OPK_N };
static const char *const OPK_NAMES[LANG_N][OPK_N] = {
    {"NIVEAU", "RATIO", "FIN", "DESACC", "ATTAQUE", "DECLIN", "TENUE", "RELACHE"},
    {"LEVEL", "RATIO", "FINE", "DETUNE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"},
    {"NIVEL", "RATIO", "FINO", "DESAFIN", "ATAQUE", "CAIDA", "SOSTEN", "SUELTA"}};
static const int8_t OPK_LO[OPK_N] = {0, 0, 0, -7, 0, 0, 0, 0}, OPK_HI[OPK_N] = {99, 31, 99, 7, 99, 99, 99, 99};

/* ---- factory sounds: a name, SOUND 1-2 and the envelope (the mix, the effect and the LFO stay) */
typedef struct {
    const char *name[LANG_N];
    uint8_t engine;
    int8_t s[8], env[4];
} preset_t;
static const preset_t PRESETS[] = {
    {{"LEAD 25", "LEAD 25", "LEAD 25"}, E_PULSE, {1, 0, 3, 15, 0, 0, 3, 0}, {0, 6, 10, 5}},
    {{"BIP 12", "BEEP 12", "BIP 12"}, E_PULSE, {0, 0, 0, 15, 0, 0, 3, 0}, {0, 4, 0, 3}},
    {{"CARRE 50", "SQUARE 50", "CUADRADA 50"}, E_PULSE, {2, 0, 0, 13, 0, 0, 3, 0}, {0, 8, 12, 5}},
    {{"SUPER CARRE", "SUPER PULSE", "SUPER PULSO"}, E_PULSE, {1, 0, 2, 14, 0, 9, 3, 0}, {0, 8, 11, 6}},
    {{"PLUCK", "PLUCK", "PLUCK"}, E_PULSE, {1, 0, 0, 12, 0, 0, 3, 0}, {0, 5, 0, 4}},
    {{"LASER", "LASER", "LASER"}, E_PULSE, {0, 12, 0, 15, 0, 0, 3, 0}, {0, 7, 0, 4}},
    {{"PWM PAD", "PWM PAD", "PWM PAD"}, E_PULSE, {4, 0, 2, 11, 0, 4, 3, 0}, {7, 9, 11, 9}},
    {{"BASSE", "BASS", "BAJO"}, E_PULSE, {2, 0, 0, 9, 0, 0, 3, 0}, {0, 7, 9, 3}},
    {{"SALE", "DIRTY", "SUCIO"}, E_PULSE, {1, 0, 0, 15, 5, 0, 3, 0}, {0, 6, 10, 4}},
    {{"BASSE NES", "NES BASS", "BAJO NES"}, E_WAVE, {0, 0, 0, 15, 0, 0, 3, 0}, {0, 8, 13, 3}},
    {{"BASSE GB", "GB BASS", "BAJO GB"}, E_WAVE, {4, 0, 0, 12, 0, 0, 3, 0}, {0, 6, 10, 3}},
    {{"SCIE", "SAW", "SIERRA"}, E_WAVE, {1, 0, 2, 13, 0, 6, 3, 0}, {0, 7, 9, 5}},
    {{"FLUTE", "FLUTE", "FLAUTA"}, E_WAVE, {2, 0, 4, 12, 0, 0, 3, 3}, {4, 8, 12, 7}},
    {{"ORGUE", "ORGAN", "ORGANO"}, E_WAVE, {5, 0, 0, 13, 0, 0, 3, 0}, {1, 9, 13, 6}},
    {{"VOIX", "VOICE", "VOZ"}, E_WAVE, {6, 0, 5, 12, 0, 3, 3, 0}, {3, 8, 11, 7}},
    {{"KICK TRI", "KICK TRI", "KICK TRI"}, E_WAVE, {0, 15, 0, 15, 0, 0, 3, 0}, {0, 4, 0, 2}},
    {{"CHARLEY", "HI-HAT", "CHARLES"}, E_NOISE, {0, 0, 0, 15, 0, 0, 3, 0}, {0, 2, 0, 2}},
    {{"CAISSE", "SNARE", "CAJA"}, E_NOISE, {0, -3, 0, 13, 0, 0, 3, 0}, {0, 5, 0, 4}},
    {{"METAL", "METAL", "METAL"}, E_NOISE, {1, 0, 0, 15, 0, 0, 3, 0}, {0, 4, 0, 3}},
    {{"VAPEUR", "STEAM", "VAPOR"}, E_NOISE, {0, 0, 0, 10, 0, 0, 3, 0}, {8, 10, 8, 9}},
    {{"EXPLOSION", "EXPLOSION", "EXPLOSION"}, E_NOISE, {0, -8, 0, 11, 0, 0, 3, 0}, {0, 12, 0, 10}},
    {{"CRASH", "CRASH", "CRASH"}, E_NOISE, {0, 0, 0, 15, 0, 0, 3, 0}, {0, 11, 0, 10}},
    {{"PIANO TINE", "TINE EP", "PIANO TINE"}, E_FM, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 5}},
    {{"CLOCHE", "BELL", "CAMPANA"}, E_FM, {1, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 9}},
    {{"BASSE FM", "FM BASS", "BAJO FM"}, E_FM, {2, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 3}},
    {{"CUIVRES", "BRASS", "METALES"}, E_FM, {3, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 5}},
    {{"NAPPE", "PAD", "COLCHON"}, E_FM, {4, 0, 0, 0, 0, 0, 3, 0}, {0, 15, 15, 9}},
    {{"MARIMBA", "MARIMBA", "MARIMBA"}, E_FM, {5, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 5}},
    {{"ORGUE FM", "FM ORGAN", "ORGANO FM"}, E_FM, {6, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 4}},
    {{"GUITARE", "PLUCK", "PUNTEO"}, E_FM, {7, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 4}},
    {{"LEAD CHIP", "CHIP LEAD", "LEAD CHIP"}, E_FM, {8, 0, 0, 0, 0, 0, 0, 4}, {0, 15, 15, 4}},
    {{"BASSE ACIDE", "ACID BASS", "BAJO ACIDO"}, E_FM, {9, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 2}},
    {{"HOOVER", "HOOVER", "HOOVER"}, E_FM, {10, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 6}},
    {{"KALIMBA", "KALIMBA", "KALIMBA"}, E_FM, {11, 0, 0, 0, 0, 0, 0, 0}, {0, 15, 15, 6}},
    {{"KIT ACOUSTIQUE", "ACOUSTIC KIT", "KIT ACUSTICO"}, E_KIT, {0, 0, 0, 0, 0, 15, 0, 0}, {0, 15, 15, 15}},
    {{"KIT HIP HOP", "HIP HOP KIT", "KIT HIP HOP"}, E_KIT, {1, 0, 0, 1, 0, 13, 0, 0}, {0, 15, 15, 15}},
    {{"KIT TECHNO", "TECHNO KIT", "KIT TECHNO"}, E_KIT, {2, 0, 0, 1, 0, 15, 0, 0}, {0, 15, 15, 15}},
    {{"KIT DUB REGGAE", "DUB REGGAE KIT", "KIT DUB REGGAE"}, E_KIT, {3, 0, 0, 0, 0, 13, 0, 0}, {0, 15, 15, 15}},
    {{"KIT LO-FI", "LO-FI KIT", "KIT LO-FI"}, E_KIT, {4, 0, 0, 1, 0, 11, 0, 0}, {0, 15, 15, 15}},
    {{"KIT GAME BOY", "GAME BOY KIT", "KIT GAME BOY"}, E_KIT, {5, 0, 0, 0, 0, 15, 0, 0}, {0, 15, 15, 15}},
    {{"KIT SNES", "SNES KIT", "KIT SNES"}, E_KIT, {6, 0, 0, 0, 0, 15, 0, 0}, {0, 15, 15, 15}},
    {{"KIT PERCUS", "PERCUSSION KIT", "KIT PERCUSION"}, E_KIT, {7, 0, 0, 0, 0, 15, 0, 0}, {0, 15, 15, 15}},
    {{"KIT GABBER", "GABBER KIT", "KIT GABBER"}, E_KIT, {2, 2, 2, 12, 0, 13, 0, 0}, {0, 15, 15, 15}},
    {{"SOLEIL", "SOLEIL", "SOLEIL"}, E_SAMPLE, {0, 0, 0, 1, 11, 2, 0, 15}, {0, 15, 15, 2}},      /* ride, live room */
    {{"GRELOT", "GRELOT", "GRELOT"}, E_SAMPLE, {1, 0, 0, 1, 11, 2, 0, 15}, {0, 15, 15, 2}},      /* 70s funk, 16th hats */
    {{"TENDU", "TENDU", "TENDU"}, E_SAMPLE, {2, 0, 0, 1, 11, 2, 0, 15}, {0, 15, 15, 2}},         /* dry, tight, claps */
    {{"ABYSSE", "ABYSSE", "ABYSSE"}, E_SAMPLE, {3, 0, 0, 1, 11, 2, 0, 15}, {0, 15, 15, 2}},      /* ragga jungle, sub */
    {{"MAMBO", "MAMBO", "MAMBO"}, E_SAMPLE, {4, 0, 0, 1, 11, 2, 0, 15}, {0, 15, 15, 2}},         /* congas, bongos, bell */
    {{"GRENIER", "GRENIER", "GRENIER"}, E_SAMPLE, {5, 0, 0, 1, 11, 2, 0, 15}, {0, 15, 15, 2}},   /* hip hop swing, dusty */
    {{"ABYSSE SALE", "ABYSSE DIRTY", "ABYSSE SUCIO"}, E_SAMPLE, {3, 0, 0, 1, 11, 10, 3, 12}, {0, 15, 15, 2}},
    {{"TENDU COURT", "TENDU SHORT", "TENDU CORTO"}, E_SAMPLE, {2, 0, 0, 1, 3, 2, 0, 15}, {0, 15, 15, 1}},
    {{"MON SAMPLE 1", "MY SAMPLE 1", "MI SAMPLE 1"}, E_SAMPLE, {(int8_t)(BRK_N + 0u), 1, 0, 0, 11, 0, 0, 15}, {0, 15, 15, 3}},
    {{"MON SAMPLE 2", "MY SAMPLE 2", "MI SAMPLE 2"}, E_SAMPLE, {(int8_t)(BRK_N + 1u), 1, 0, 0, 11, 0, 0, 15}, {0, 15, 15, 3}},
    {{"MON SAMPLE 3", "MY SAMPLE 3", "MI SAMPLE 3"}, E_SAMPLE, {(int8_t)(BRK_N + 2u), 1, 0, 0, 11, 0, 0, 15}, {0, 15, 15, 3}},
    {{"MON SAMPLE 4", "MY SAMPLE 4", "MI SAMPLE 4"}, E_SAMPLE, {(int8_t)(BRK_N + 3u), 1, 0, 0, 11, 0, 0, 15}, {0, 15, 15, 3}},
};
#define KIT_DEMO 6                              /* the demo's kit (its first knob): SNES */
#define BRK_DEMO 3                              /* the demo's break: ABYSSE (the boss is breakcore) */
#define NPRESET ((uint8_t)(sizeof(PRESETS) / sizeof(PRESETS[0])))
/* each track's sound on a new project (an index in PRESETS) */
static const uint8_t TRK_ENGINE0[NTRK] = {E_PULSE, E_FM, E_WAVE, E_NOISE, E_KIT, E_SAMPLE};   /* a new project: lead, */
static const int8_t TRK_S0[NTRK] = {1, 0, 0, 0, 0, 0};   /* FM, bass, hats, kit, break (each its engine's sound with this */
                                                          /* first knob: LEAD 25, TINE EP, NES BASS, HI-HAT, ACOUSTIC, SOLEIL) */

/* ---- notes */
static const char *const NOTE_NAME[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
#define NOTE_NONE 0u
#define NOTE_OFF 255u
#define KEY0 53u                                /* key 0 = F3 */
static int is_black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }
static int8_t WHITE_OF_KEY[27], BLACK_OF_KEY[27];    /* key -> its index among the white / black keys, or -1 */
static uint8_t KEY_OF_WHITE[16], KEY_OF_BLACK[11];
static void keys_init(void)
{
    uint32_t k, w = 0, b = 0;
    for (k = 0; k < 27u; k++) {
        WHITE_OF_KEY[k] = BLACK_OF_KEY[k] = -1;
        if (is_black(KEY0 + k)) { BLACK_OF_KEY[k] = (int8_t)b; KEY_OF_BLACK[b++] = (uint8_t)k; }
        else { WHITE_OF_KEY[k] = (int8_t)w; KEY_OF_WHITE[w++] = (uint8_t)k; }
    }
}

/* ---- the cell tricks (FX column): a type and a value */
enum { FX_NONE, FX_ARP, FX_GLIDE, FX_VIB, FX_ROLL, FX_BEND, FX_CHANCE, FX_DELAY, FX_CUT, FX_REV, FX_PITCH, FX_SHAPE,
       FX_CRUSH, FX_VSL, FX_PAN, FX_OFS, FX_HOP, FX_END, FX_N };   /* (HOP and END: the sequencer's, format 5) */
typedef struct {
    const char *tag[LANG_N];                    /* three letters */
    uint8_t icon;
    int8_t lo, hi, def;
} fxdesc_t;
static const fxdesc_t FXD[FX_N] = {
    {{"---", "---", "---"}, ICO_ERASE, 0, 0, 0},
    {{"ARP", "ARP", "ARP"}, ICO_ARP, 0, 13, 1},             /* arpeggio */
    {{"GLI", "GLI", "GLI"}, ICO_GLIDE, 1, 15, 6},           /* glide */
    {{"VIB", "VIB", "VIB"}, ICO_VIB, 1, 15, 5},             /* vibrato */
    {{"ROL", "ROL", "RED"}, ICO_ROLL, 0, 8, 2},             /* roll (redoble) */
    {{"BND", "BND", "BND"}, ICO_BEND, -15, 15, -6},         /* bend */
    {{"CHN", "CHN", "AZR"}, ICO_DICE, 1, 9, 5},             /* chance (azar) */
    {{"RET", "DLY", "RET"}, ICO_CLOCK, 1, 23, 12},          /* delay (retard, retraso) */
    {{"CUT", "CUT", "CRT"}, ICO_CUT, 1, 23, 12},            /* cut (coupe, corte) */
    {{"ENV", "REV", "REV"}, ICO_REV, 0, 0, 0},              /* reverse (envers, reves) */
    {{"HAU", "PIT", "ALT"}, ICO_SHARP, -12, 12, 12},        /* pitch (hauteur, altura) */
    {{"FOR", "SHP", "FOR"}, ICO_SHAPE, 0, 7, 0},            /* shape (forme, forma) */
    {{"ECR", "CRU", "TRI"}, ICO_CRUSH, 1, 7, 4},            /* crush (ecrase, tritura) */
    {{"VOL", "VSL", "VOL"}, ICO_VOL, -15, 15, -4},          /* volume slide over the row (an empty cell: the note playing) */
    {{"PAN", "PAN", "PAN"}, ICO_MIRROR, -7, 7, -5},         /* this note's pan */
    {{"DEP", "OFS", "INI"}, ICO_RIGHT, 0, 15, 8},           /* SAMPLE: start further in (n / 16 of the slice; depart, inicio) */
    {{"SAU", "HOP", "SAL"}, ICO_LOOP, 0, 31, 0},            /* after this row, row n + 1 (saut, salto) */
    {{"FIN", "END", "FIN"}, ICO_STOP, 0, 0, 0},             /* the pattern ends after this row */
};
/* the tricks of a table's steps (TABLE): each acts on the note playing when its step comes */
enum { TB_NONE, TB_HIT, TB_CUT, TB_VIB, TB_BEND, TB_SHAPE, TB_CRUSH, TB_VSL, TB_PAN, TB_HOP, TB_N };
static const fxdesc_t TBD[TB_N] = {
    {{"---", "---", "---"}, ICO_ERASE, 0, 0, 0},
    {{"FRP", "HIT", "GOL"}, ICO_AGAIN, 0, 0, 0},            /* the note again (frappe, golpe), its step's pitch */
    {{"CUT", "CUT", "CRT"}, ICO_CUT, 0, 0, 0},              /* the note stops */
    {{"VIB", "VIB", "VIB"}, ICO_VIB, 0, 15, 5},             /* vibrato from here (0: none) */
    {{"BND", "BND", "BND"}, ICO_BEND, -15, 15, -6},         /* the pitch bends from here (0: no more) */
    {{"FOR", "SHP", "FOR"}, ICO_SHAPE, 0, 7, 0},            /* the wave's shape */
    {{"ECR", "CRU", "TRI"}, ICO_CRUSH, 0, 7, 4},            /* crush (0: none) */
    {{"VOL", "VSL", "VOL"}, ICO_VOL, -15, 15, -4},          /* the volume slides (0: it stops) */
    {{"PAN", "PAN", "PAN"}, ICO_MIRROR, -7, 7, -5},         /* the note's pan */
    {{"SAU", "HOP", "SAL"}, ICO_LOOP, 0, 15, 0},            /* to step n + 1 (after this one plays its time) */
};
static const uint8_t GROOVE_PRESET[6][17] = {   /* the groove's presets (GROOVE, KNOB 3): length, then the ticks */
    {2, 6, 6}, {2, 7, 5}, {2, 8, 4}, {3, 4, 4, 4}, {4, 7, 5, 6, 6}, {4, 9, 3, 8, 4}};
static const char *const GROOVE_NAMES[LANG_N][6] = {
    {"DROIT", "SWING 58", "SWING 67", "TRIOLET", "DECALE", "SHUFFLE"},
    {"STRAIGHT", "SWING 58", "SWING 67", "TRIPLET", "LAZY", "SHUFFLE"},
    {"RECTO", "SWING 58", "SWING 67", "TRESILLO", "PEREZOSO", "SHUFFLE"}};
static const uint8_t ROLL_N[9] = {2, 3, 4, 6, 8, 12, 16, 24, 32};
typedef struct {
    const char *name[LANG_N];
    int8_t n, off[4];
} chord_t;
static const chord_t CHORDS[14] = {
    {{"MAJ", "MAJ", "MAY"}, 3, {0, 4, 7, 0}}, {{"MIN", "MIN", "MEN"}, 3, {0, 3, 7, 0}}, {{"OCT", "OCT", "OCT"}, 2, {0, 12, 0, 0}},
    {{"5TE", "5TH", "5TA"}, 2, {0, 7, 0, 0}}, {{"SUS", "SUS", "SUS"}, 3, {0, 5, 7, 0}}, {{"7", "7", "7"}, 4, {0, 4, 7, 10}},
    {{"MA7", "MA7", "MA7"}, 4, {0, 4, 7, 11}}, {{"MI7", "MI7", "ME7"}, 4, {0, 3, 7, 10}}, {{"DIM", "DIM", "DIS"}, 3, {0, 3, 6, 0}},
    {{"AUG", "AUG", "AUM"}, 3, {0, 4, 8, 0}}, {{"2OC", "2OC", "2OC"}, 3, {0, 12, 24, 0}}, {{"AD9", "AD9", "AD9"}, 4, {0, 4, 7, 14}},
    {{"TRI", "TRL", "TRI"}, 2, {0, 1, 0, 0}}, {{"BAS", "LOW", "BAJ"}, 2, {0, -12, 0, 0}},
};

/* ---- scales (bit n: semitone n above the root) */
typedef struct {
    const char *name[LANG_N];
    uint16_t mask;
} scale_t;
static const scale_t SCALES[12] = {
    {{"CHROMA", "CHROMATIC", "CROMATICA"}, 0xFFF}, {{"MAJEUR", "MAJOR", "MAYOR"}, 0xAB5},
    {{"MINEUR", "MINOR", "MENOR"}, 0x5AD}, {{"PENTA", "PENTA", "PENTA"}, 0x295},
    {{"PENTA MIN", "PENTA MIN", "PENTA MEN"}, 0x4A9}, {{"BLUES", "BLUES", "BLUES"}, 0x4E9},
    {{"DORIEN", "DORIAN", "DORICA"}, 0x6AD}, {{"PHRYGIEN", "PHRYGIAN", "FRIGIA"}, 0x5AB},
    {{"HARMONIQ", "HARMONIC", "ARMONICA"}, 0x9AD}, {{"JAPON", "JAPANESE", "JAPONESA"}, 0x1A3},
    {{"ARABE", "ARABIC", "ARABE"}, 0x9B3}, {{"PAR TONS", "WHOLE TONE", "TONOS"}, 0x555},
};

/* ---- live effects: hold FX (or tap it to keep the page open), then the white keys. Two banks of 16:
 * ids 0..31 (bit id of a mask); the LIVE lane of a pattern stores id + 1 (0: none). They play on the
 * master or on one track (FX page, KNOB 4: TARGET) */
enum { LF_LOOP16, LF_LOOP12, LF_LOOP32, LF_LOOP64, LF_UNI, LF_UNILO, LF_OCTUP, LF_OCTDN,
       LF_STUT4, LF_STUT3, LF_SCR, LF_SCRF, LF_TRIP, LF_AGAIN, LF_REV, LF_ROLLUP,
       LF_CRUSH, LF_DECIM, LF_LPF, LF_HPF, LF_TAPE, LF_DIVE, LF_GATE16, LF_GATE32,
       LF_FREEZE, LF_RING, LF_ECHO, LF_DIST, LF_WOB, LF_VINYL, LF_NOMEL, LF_NOBEAT, LF_N };
typedef struct {
    const char *name[LANG_N], *sub[LANG_N];
    uint8_t icon;
} lfx_t;
static const lfx_t LFX[LF_N] = {
    {{"BOUCLE", "LOOP", "BUCLE"}, {"1/16", "1/16", "1/16"}, ICO_LOOP},
    {{"BOUCLE", "LOOP", "BUCLE"}, {"1/12", "1/12", "1/12"}, ICO_LOOP},
    {{"BOUCLE", "LOOP", "BUCLE"}, {"1/32", "1/32", "1/32"}, ICO_LOOP},
    {{"BOUCLE", "LOOP", "BUCLE"}, {"1/64", "1/64", "1/64"}, ICO_LOOP},
    {{"UNISSON", "UNISON", "UNISONO"}, {"", "", ""}, ICO_UNI},
    {{"UNISSON", "UNISON", "UNISONO"}, {"BAS", "LOW", "GRAVE"}, ICO_UNI},
    {{"OCTAVE", "OCTAVE", "OCTAVA"}, {"+1", "+1", "+1"}, ICO_UP},
    {{"OCTAVE", "OCTAVE", "OCTAVA"}, {"-1", "-1", "-1"}, ICO_DOWN},
    {{"BEGAIE", "STUTTER", "TARTAMU"}, {"X4", "X4", "X4"}, ICO_STUT},
    {{"BEGAIE", "STUTTER", "TARTAMU"}, {"X3", "X3", "X3"}, ICO_STUT},
    {{"SCRATCH", "SCRATCH", "SCRATCH"}, {"", "", ""}, ICO_DISC},
    {{"SCRATCH", "SCRATCH", "SCRATCH"}, {"VITE", "FAST", "RAPIDO"}, ICO_DISC},
    {{"6/8", "6/8", "6/8"}, {"TRIOLET", "TRIPLET", "SWING"}, ICO_TRI3},
    {{"RELANCE", "RETRIG", "REPITE"}, {"1 TEMPS", "1 BEAT", "1 PULSO"}, ICO_AGAIN},
    {{"ENVERS", "REVERSE", "REVES"}, {"", "", ""}, ICO_REV},
    {{"ROULEAU", "BUILDUP", "REDOBLE"}, {"ACCEL", "ROLL", "ACELERA"}, ICO_ACCEL},
    {{"ECRASE", "CRUSH", "TRITURA"}, {"4 BITS", "4 BITS", "4 BITS"}, ICO_CRUSH},
    {{"DECIME", "LO-FI", "DIEZMA"}, {"", "RATE", ""}, ICO_PIXEL},
    {{"FILTRE", "LOWPASS", "FILTRO"}, {"BAS", "SWEEP", "GRAVES"}, ICO_LPF},
    {{"FILTRE", "HIPASS", "FILTRO"}, {"HAUT", "SWEEP", "AGUDOS"}, ICO_HPF},
    {{"STOP", "TAPE", "CINTA"}, {"K7", "STOP", "PARADA"}, ICO_TAPE},
    {{"PLONGEE", "DIVE", "PICADO"}, {"", "", ""}, ICO_DIVE},
    {{"HACHE", "GATE", "GATE"}, {"1/16", "1/16", "1/16"}, ICO_GATE},
    {{"HACHE", "GATE", "GATE"}, {"1/32", "1/32", "1/32"}, ICO_GATE},
    {{"GEL", "FREEZE", "CONGELA"}, {"", "", ""}, ICO_SNOW},
    {{"ANNEAU", "RING", "ANILLO"}, {"", "", ""}, ICO_RING},
    {{"ECHO", "ECHO", "ECO"}, {"", "", ""}, ICO_ECHO},
    {{"DISTO", "DISTORT", "DISTOR"}, {"", "", ""}, ICO_DIST},
    {{"WOBBLE", "WOBBLE", "WOBBLE"}, {"", "", ""}, ICO_WOB},
    {{"VINYLE", "VINYL", "VINILO"}, {"", "", ""}, ICO_DISC},
    {{"SANS", "NO", "SIN"}, {"MELO", "MELODY", "MELODIA"}, ICO_NOMEL},
    {{"SANS", "NO", "SIN"}, {"BEAT", "BEAT", "RITMO"}, ICO_NOBEAT},
};
static const char *const LBANK_NAMES[LANG_N][2] = {{"POCHE", "CASSE"}, {"POCKET", "WRECK"}, {"BOLSA", "ROTO"}};
#define LBANK_NAME LBANK_NAMES[lang]
static const uint16_t LBANK_COL[2] = {RGB(255, 120, 200), RGB(255, 110, 70)};

/* ---- the magic tools: tap LFO (MAGIE), then a white key; one undo each (SAVE held) */
enum { MG_CHOP, MG_BEAT, MG_MELO, MG_BASS, MG_ARP, MG_EUCL, MG_VARY, MG_ROLLS, MG_NUANCE, MG_DOUBLE, MG_SHIFT,
       MG_CLONE, MG_TRANSP, MG_COPY, MG_PASTE, MG_CLEAR, MG_N };
static const lfx_t MAGIC[MG_N] = {
    {{"HACHOIR", "CHOPPER", "PICADOR"}, {"BREAK", "BREAK", "BREAK"}, ICO_KNIFE},
    {{"BEAT", "BEAT", "RITMO"}, {"HASARD", "RANDOM", "AZAR"}, ICO_KIT},
    {{"MELODIE", "MELODY", "MELODIA"}, {"GAMME", "SCALE", "ESCALA"}, ICO_NOTE},
    {{"BASSE", "BASS", "BAJO"}, {"GAMME", "SCALE", "ESCALA"}, ICO_BASS},
    {{"ARPEGE", "ARP", "ARPEGIO"}, {"ACCORDS", "CHORDS", "ACORDES"}, ICO_ARP},
    {{"EUCLIDE", "EUCLID", "EUCLIDE"}, {"N COUPS", "N HITS", "GOLPES"}, ICO_EUCL},
    {{"VARIER", "VARY", "VARIAR"}, {"UN PEU", "A BIT", "UN POCO"}, ICO_DICE},
    {{"ROULES", "ROLLS", "REDOBLE"}, {"FINS", "ENDS", "FINAL"}, ICO_ROLL},
    {{"NUANCES", "ACCENTS", "MATICES"}, {"VOLUMES", "VOLUMES", "VOLUMEN"}, ICO_VOL},
    {{"DOUBLER", "DOUBLE", "DUPLICA"}, {"X2", "X2", "X2"}, ICO_X2},
    {{"DECALER", "SHIFT", "MOVER"}, {"1 PAS", "1 STEP", "1 PASO"}, ICO_RIGHT},
    {{"CLONER", "CLONE", "CLONAR"}, {"MOTIF", "PATTERN", "PATRON"}, ICO_CLONE},
    {{"TRANSPO", "TRANSP", "TRANSP"}, {"+1 NOTE", "+1 NOTE", "+1 NOTA"}, ICO_UPDN},
    {{"COPIER", "COPY", "COPIAR"}, {"", "", ""}, ICO_COPY},
    {{"COLLER", "PASTE", "PEGAR"}, {"", "", ""}, ICO_PASTE},
    {{"EFFACER", "CLEAR", "BORRAR"}, {"", "", ""}, ICO_ERASE},
};
static const char *const L_SCOPES[LANG_N][2] = {{"PISTE", "MOTIF"}, {"TRACK", "PATTERN"}, {"PISTA", "PATRON"}};
#define L_SCOPE L_SCOPES[lang]

/* ---- names for new projects */
static const char *const NAME_A[8] = {"KAWA", "MIETTE", "PIXEL", "BOUM", "ZINZIN", "CHIP", "RAGE", "CANDY"};
static const char *const NAME_B[8] = {"ROSE", "VOLT", "NOIRE", "FOLLE", "MAGIK", "TURBO", "DOUCE", "SALE"};

/* a list's value in the language (K_LIST: n values x 3 languages; K_NAMES: [3][n], here FM_PATCH_NAME) */
static const char *pd_value(const pdesc_t *d, int32_t v)
{
    uint32_t n = (uint32_t)(d->hi - d->lo + 1);
    v = clampi(v, d->lo, d->hi);
    if (d->kind == K_NAMES)
        return d->list[(uint32_t)lang * FM_NPATCH + (uint32_t)(v - d->lo)];
    return d->list[(uint32_t)lang * n + (uint32_t)(v - d->lo)];
}
