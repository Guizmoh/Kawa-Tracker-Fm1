/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the project (patterns, sounds, song, master) and what the audio interrupt and the main loop share.
 *
 * A pattern is NROW rows of NTRK cells (note, volume, two tricks and their values) plus the LIVE lane: the
 * live effect recorded on each row. A project is 32 patterns, the sound of each track (its engine and 24
 * parameters), its table (steps played from each note) and its sample's edit, the song (a list of
 * patterns, each played N times), the master effect, the tempo, the swing or the groove, and the scale.
 * It is all in RAM; kawa_edit.c keeps it in flash. */
#define NROW 32u
#define NPAT 32u
#define NSONG 64u
#define CHUNK 16u                               /* control rate: envelopes, pitch, sequencer every 16 frames */

typedef struct __attribute__((packed)) {      /* 5 bytes (format 5; 4 before: note, vol, fx, fxv) */
    uint8_t note;                               /* 0 empty, 255 OFF, else MIDI note / drum + 1 / slice + 1 */
    uint16_t vol : 4, fx : 6, fx2 : 6;          /* the volume 1..15; two tricks (FX_...), both played */
    uint8_t fxv, fxv2;                          /* their values (int8_t for the signed ones) */
} cell_t;
typedef char cell_is_5_bytes[sizeof(cell_t) == 5u ? 1 : -1];
typedef struct {
    cell_t c[NROW][NTRK];
    uint8_t live[NROW];                         /* a live effect per row (LF_... + 1), 0 = none */
    uint8_t len, pad[3];                        /* rows played, 1..32 */
} pat_t;
typedef struct {
    uint8_t engine, preset;
    int8_t p[NPARAM];
    uint8_t pad[2];
} sound_t;
typedef struct {
    uint8_t pat, rep;                           /* a song entry: the pattern, played rep times (1..16) */
} song_t;
/* a track's table (M8's): steps played one after the other from each note, every SPEED ticks (6 a row) */
#define NTSTEP 16u
typedef struct {
    int8_t tr;                                  /* semitones over the note (KIT, slices: drums / slices over it) */
    uint8_t vol;                                /* 1..15; 0 ("--"): as it is */
    uint8_t fx;                                 /* a trick of the table (TB_...) */
    int8_t fxv;
} tstep_t;
typedef struct {
    tstep_t s[NTSTEP];
    uint8_t on, speed, len, loop;               /* speed 1..24 ticks a step; len 1..16; loop: the step it goes back
                                                 * to after the last (NTSTEP: none, the last step stays) */
} table_t;
/* a track's sample, edited (nothing is written into the sound: these say how it plays) */
typedef struct {
    uint16_t start, end;                        /* where it plays, in 65536ths of the sound */
    uint16_t sl[16];                            /* the slices' starts (MANUAL), in 65536ths; else 16 even ones */
    uint16_t gain;                              /* Q8: 256 as recorded (NORMALISE sets it from the peak) */
    uint8_t rev, manual, norm, pad;
} sedit_t;
/* the groove (M8's): the ticks of each row in turn (6 a straight 16th), in place of the swing when on */
typedef struct {
    uint8_t tick[16];                           /* 1..12 */
    uint8_t len, on, pad[2];                    /* len 1..16 */
} groove_t;
#define PRJ_MAGIC 0x4157414Bu                   /* "KAWA" */
#define PRJ_VERSION 5u
typedef struct {
    uint32_t magic, version;
    char name[16];
    uint16_t bpm;
    uint8_t swing, root, scale, lock, mode, song_len;   /* swing 50..75 %; mode 0 loop, 1 song */
    song_t song[NSONG];
    sound_t snd[NTRK];
    uint8_t mute, step;
    int8_t mfx[4];                              /* the master's effect: type, A, B, mix */
    uint8_t home, unsaved;                      /* the user's slot it was saved to or loaded from (0: none); changes
                                                 * not saved into it (the autosave keeps both: they come back at boot) */
    uint8_t fmp[156];                           /* the FM track's patch (its operators, edited in EDIT) */
    pat_t pat[NPAT];
    table_t tbl[NTRK];                          /* (format 5 from here) */
    sedit_t sed[NTRK];
    groove_t grv;
    uint32_t crc;
} project_t;

#if defined(DINGHY_HOST) || defined(KAWA_WASM)
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif
static project_t prj POOL;

/* the drum or the slice of a cell on a KIT track / a SAMPLE track in slice mode */
static uint32_t cell_idx(uint32_t note) { return note - 1u; }
/* how a track reads its notes: MIDI notes, drums 1..16, slices 1..16 */
enum { NM_NOTE, NM_DRUM, NM_SLICE };
static uint32_t note_mode(uint32_t t)
{
    uint32_t e = prj.snd[t].engine;
    if (e == E_KIT) return NM_DRUM;
    if (e == E_SAMPLE && prj.snd[t].p[P_SMODE] == 0) return NM_SLICE;
    return NM_NOTE;
}
#define IS_TONAL(t) (note_mode(t) == NM_NOTE)
static uint32_t fm_track(void)                  /* the one track that plays FM (NTRK: none) */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (prj.snd[t].engine == E_FM)
            return t;
    return NTRK;
}

/* ---- shared with the audio interrupt (one writer each) */
static volatile uint8_t sq_play;                /* the sequencer runs (ISR) */
static volatile uint8_t sq_req;                 /* main -> ISR: 1 start, 2 stop */
static volatile uint8_t sq_pat, sq_row;         /* ISR: what plays now */
static volatile uint8_t sq_next = 0xFF;         /* main: the pattern to go to at the end of this one (loop mode) */
static volatile int32_t sq_adj;                 /* main: song entries inserted (+1) or deleted (-1) before the one
                                                 * playing, all told (the ISR moves its position as much) */
static volatile uint8_t sq_goto = 0xFF;         /* main: the entry to go to at the end of this pattern (the one playing
                                                 * was deleted); the ISR takes it */
static volatile uint8_t sq_songpos, sq_rep;     /* ISR: the song position playing, and its repeat so far */
static volatile uint8_t sq_start_pat, sq_start_pos;   /* main: where a start begins */
static volatile uint32_t sq_rowtick;            /* ISR: counts rows played (the screen follows it) */
static volatile uint32_t clk_ticks;             /* ISR: MIDI clock ticks due (24 a beat, as played, swing too) */
static volatile uint8_t clk_start, clk_stop;    /* ISR: the transport started / stopped (MIDI start / stop) */
static volatile uint32_t sq_frac;               /* ISR: the phase in the row, 0..2^32 */
static volatile uint32_t trig_ms[NTRK];         /* ISR: when each track last played a note (the cells flash) */
static volatile uint16_t level[NTRK + 1];       /* ISR: peak per track and the master, the last block (MIX) */
static volatile uint8_t fm_trk = NTRK;         /* main: the FM track (only one: the audio interrupt's budget) */
static volatile uint16_t env_now[NTRK];         /* ISR: each track's envelope now (the ENV page's dot), Q16 */
static volatile uint8_t env_stage[NTRK];
static volatile uint32_t lfx_keys;              /* main: live effects held (bit LF_... ) */
static volatile uint32_t lfx_on;                /* ISR: live effects sounding now */
static volatile uint8_t lfx_target;             /* main: 0 the master, 1..6 a track */
static volatile uint8_t live_mute;              /* main: tracks muted from the FX page (bit per track) */
static volatile uint8_t rec_live;               /* main: write live effects into the LIVE lane while playing */
static volatile uint32_t audio_us;              /* ISR: the last render's time */
static volatile uint32_t audio_late;            /* ISR: blocks cut short by the deadline guard (OPTIONS) */
static volatile uint8_t cpu_shed;               /* ISR: the load governor's level, 0 (all) .. 3 (the least work) */
static volatile uint8_t cpu_pct, cpu_max;       /* ISR: the render's share of its 5.8 ms, now and at most (OPTIONS) */
static int16_t scope[240];                      /* ISR: the live effects' bus, decimated, for the screens */
static volatile uint32_t scope_w;
static volatile uint8_t tb_now[NTRK];           /* ISR: the step each track's table plays (TABLE), NTSTEP: none */
static volatile uint8_t clk_in;                 /* main: the tempo comes from the MIDI clock in (OPTIONS CLOCK IN) */
static volatile uint32_t clk_in_ticks;          /* main: MIDI clock ticks received since the start (FA) */
static volatile uint32_t clk_in_tick;           /* main: frames a tick in 16ths of a frame, measured over the last
                                                 * four beats (0: none yet) */
static uint32_t g_rowlen = 3845u;               /* ISR: frames per row (16th) at the tempo, without swing */
static uint32_t g_ticklen = 640u;               /* ISR: frames per tick (a row is 6): the tables' and the groove's */
static uint32_t g_beatpos;                      /* ISR: frames since the last beat (the live effects lock to it) */

/* a cell's two tricks, as the voices take them */
typedef struct {
    uint8_t fx, fx2;
    int8_t v, v2;
} tricks_t;
static int trick(const tricks_t *k, uint32_t f, int32_t *v)   /* the cell plays trick f: its value (the second */
{                                                              /* column's when both have it) */
    if (k->fx2 == f) { *v = k->v2; return 1; }
    if (k->fx == f) { *v = k->v; return 1; }
    return 0;
}

/* main -> ISR: notes played on the keys (or a cell to hear), a small queue */
typedef struct {
    uint8_t type, trk, note, vol, fx, fxv, fx2, fxv2;
} ev_t;
enum { EV_ON = 1, EV_OFF, EV_ALLOFF, EV_KILL };   /* EV_KILL: a track's voices go now (its engine changed; trk NTRK: all) */
#define EVQ 32u
static ev_t evq[EVQ];
static volatile uint32_t evq_w, evq_r;
static void ev_push2(uint32_t type, uint32_t trk, uint32_t note, uint32_t vol, uint32_t fx, uint32_t fxv, uint32_t fx2,
                     uint32_t fxv2)
{
    ev_t *e;
    if (evq_w - evq_r >= EVQ)
        return;
    e = &evq[evq_w % EVQ];
    e->type = (uint8_t)type;
    e->trk = (uint8_t)trk;
    e->note = (uint8_t)note;
    e->vol = (uint8_t)vol;
    e->fx = (uint8_t)fx;
    e->fxv = (uint8_t)fxv;
    e->fx2 = (uint8_t)fx2;
    e->fxv2 = (uint8_t)fxv2;
    RING_PUBLISH();
    evq_w++;
}
static void ev_push(uint32_t type, uint32_t trk, uint32_t note, uint32_t vol, uint32_t fx, uint32_t fxv)
{
    ev_push2(type, trk, note, vol, fx, fxv, FX_NONE, 0);
}

/* random numbers: one generator for the ISR, one for the main loop */
static uint32_t rnd_isr = 0x9E3779B9u, rnd_main = 0x1234567u;
static uint32_t xr(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static uint32_t rnd(uint32_t n) { return n ? (xr(&rnd_main) >> 8) % n : 0u; }

/* ---- user samples: four slots in flash (9 sectors each from USR_AT, kawa_edit.c), 8-bit mu-law, 22050 or
 * 11025 Hz. A slot: its header in the first 256 bytes (written last, after its CRC is checked), the sound from
 * USR_DATA. The audio interrupt reads them where they are (memory-mapped flash, as Felucca's samples) */
#define USR_SIZE 0x9000u                        /* 36 KiB a slot */
#define USR_DATA 0x100u
#define USR_CAP (USR_SIZE - USR_DATA)           /* 36608 B of sound: 1.66 s at 22 kHz, 3.3 s at 11 kHz */
#define USR_MAGIC 0x504D534Bu                   /* "KSMP" */
typedef struct {
    uint32_t magic, len, rate;                  /* len in samples; rate 22050 or 11025 */
    char name[12];
    uint32_t crc, rsv[2];                       /* crc: CRC-32 (zlib's) of the len bytes of sound */
} usr_hdr_t;
static usr_hdr_t usr_hdr[NUSR];                 /* main: read at boot and after an import */
static const uint8_t *volatile usr_data[NUSR];  /* main: where each slot's sound is (0: empty) */
static volatile uint8_t smp_kill;               /* main -> ISR: stop every sample voice (a slot is rewritten) */
static volatile uint8_t audio_hold;             /* main: the audio interrupt renders nothing (flash is written) */
static volatile uint32_t audio_blocks;          /* ISR: counts the blocks (the main loop waits on it) */
