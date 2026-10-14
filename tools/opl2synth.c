/* Small YM3812 (OPL2) FM synthesizer for rendering register logs to audio.
 *
 * Usage: opl2synth <events.bin> <out.f32>
 *   events.bin: uint32 total_samples, then records {uint32 sample; uint8 reg; uint8 val}
 *               sorted by sample. Rendered at the chip's native rate, 49716 Hz.
 *   out.f32:    mono float32 samples (unnormalised).
 *
 * Model: 18 operators with ADSR envelopes (datasheet timings), TL/KSL/KSR, 4 waveforms,
 * feedback, FM/additive connection, tremolo/vibrato, and rhythm mode (BD/SD/TOM/CYM/HH with
 * the noise LFSR and phase tricks of the real chip). It is an approximation of the hardware,
 * not a cycle-exact emulator.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 49716.0
#define PI 3.14159265358979323846

enum { EG_OFF, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

typedef struct {
    int am, vib, egt, ksr, mult, ksl, tl, ar, dr, sl, rr, ws;
    uint32_t phase;     /* 20-bit fraction of a cycle in the top bits of a 30-bit counter */
    double env;         /* attenuation in 0.1875 dB units, 0..511 */
    int stage, key;
    double out, prev;
} Op;

typedef struct { int fnum, block, key, fb, cnt; } Chan;

static Op op[18];
static Chan ch[9];
static int wse, rhythm_bd, am_depth, vib_depth;
static uint32_t noise = 1;
static const int mult2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
static const int kslrom[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};
static const int kslshift[4] = {8, 1, 2, 0};
/* operator register offset -> operator index (-1 = none) */
static int off2op(int off) {
    static const int map[22] = {0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, 12, 13, 14, 15, 16, 17};
    return off < 22 ? map[off] : -1;
}
/* channel -> modulator / carrier operator index */
static const int chmod[9] = {0, 1, 2, 6, 7, 8, 12, 13, 14};
static const int chcar[9] = {3, 4, 5, 9, 10, 11, 15, 16, 17};
static int opchan[18];

static void key_on(Op *o) {
    if (!o->key) { o->phase = 0; o->stage = EG_ATTACK; }
    o->key = 1;
}
static void key_off(Op *o) {
    if (o->key && o->stage != EG_OFF) o->stage = EG_RELEASE;
    o->key = 0;
}

static void update_keys(void) {
    for (int c = 0; c < 9; c++) {
        int k = ch[c].key;
        int km = k, kc = k;
        if (rhythm_bd & 0x20) {
            if (c == 6) { km |= (rhythm_bd >> 4) & 1; kc |= (rhythm_bd >> 4) & 1; }
            if (c == 7) { km |= rhythm_bd & 1; kc |= (rhythm_bd >> 3) & 1; }
            if (c == 8) { km |= (rhythm_bd >> 2) & 1; kc |= (rhythm_bd >> 1) & 1; }
        }
        Op *m = &op[chmod[c]], *r = &op[chcar[c]];
        if (km) key_on(m); else key_off(m);
        if (kc) key_on(r); else key_off(r);
    }
}

static void write_reg(int reg, int val) {
    int o;
    if (reg == 0x01) { wse = val & 0x20; return; }
    if (reg == 0xBD) { am_depth = val >> 7; vib_depth = (val >> 6) & 1; rhythm_bd = val & 0x3F; update_keys(); return; }
    if (reg >= 0x20 && reg <= 0x35 && (o = off2op(reg - 0x20)) >= 0) {
        op[o].am = val >> 7; op[o].vib = (val >> 6) & 1; op[o].egt = (val >> 5) & 1;
        op[o].ksr = (val >> 4) & 1; op[o].mult = val & 15; return;
    }
    if (reg >= 0x40 && reg <= 0x55 && (o = off2op(reg - 0x40)) >= 0) { op[o].ksl = val >> 6; op[o].tl = val & 63; return; }
    if (reg >= 0x60 && reg <= 0x75 && (o = off2op(reg - 0x60)) >= 0) { op[o].ar = val >> 4; op[o].dr = val & 15; return; }
    if (reg >= 0x80 && reg <= 0x95 && (o = off2op(reg - 0x80)) >= 0) { op[o].sl = val >> 4; op[o].rr = val & 15; return; }
    if (reg >= 0xE0 && reg <= 0xF5 && (o = off2op(reg - 0xE0)) >= 0) { op[o].ws = val & 3; return; }
    if (reg >= 0xA0 && reg <= 0xA8) { ch[reg - 0xA0].fnum = (ch[reg - 0xA0].fnum & 0x300) | val; return; }
    if (reg >= 0xB0 && reg <= 0xB8) {
        Chan *c = &ch[reg - 0xB0];
        c->fnum = (c->fnum & 0xFF) | ((val & 3) << 8);
        c->block = (val >> 2) & 7;
        c->key = (val >> 5) & 1;
        update_keys();
        return;
    }
    if (reg >= 0xC0 && reg <= 0xC8) { ch[reg - 0xC0].fb = (val >> 1) & 7; ch[reg - 0xC0].cnt = val & 1; return; }
}

static double wave(int ws, double ph) {          /* ph in cycles */
    double p = ph - floor(ph), s = sin(2 * PI * p);
    switch (wse ? ws : 0) {
    case 1: return p < 0.5 ? s : 0;
    case 2: return fabs(s);
    case 3: return (p < 0.25 || (p >= 0.5 && p < 0.75)) ? fabs(s) : 0;
    default: return s;
    }
}

/* Envelope step for effective rate er (0..63): units per sample. */
static double rate_units(int er) {
    if (er <= 0) return 0;
    if (er > 63) er = 63;
    int hi = er >> 2, lo = er & 3;
    double ms = 39280.64 / pow(2.0, hi - 1) * 4.0 / (4 + lo);   /* time for 96 dB */
    return 512.0 / (ms / 1000.0 * RATE);
}
static double attack_coef(int er) {
    if (er <= 0) return 0;
    if (er >= 60) return 1;
    int hi = er >> 2, lo = er & 3;
    double ms = 2826.24 / pow(2.0, hi - 1) * 4.0 / (4 + lo);
    return 1 - exp(-6.24 / (ms / 1000.0 * RATE));
}

static int eff_rate(Op *o, int r, Chan *c) {
    if (!r) return 0;
    int ks = (c->block << 1) | ((c->fnum >> 9) & 1);
    if (!o->ksr) ks >>= 2;
    int er = r * 4 + ks;
    return er > 63 ? 63 : er;
}

static void env_step(Op *o, Chan *c) {
    switch (o->stage) {
    case EG_ATTACK: {
        double k = attack_coef(eff_rate(o, o->ar, c));
        o->env -= (o->env + 1) * k;
        if (o->env <= 0) { o->env = 0; o->stage = EG_DECAY; }
        break;
    }
    case EG_DECAY: {
        double slv = o->sl == 15 ? 496 : o->sl * 16;
        o->env += rate_units(eff_rate(o, o->dr, c));
        if (o->env >= slv) { o->env = slv; o->stage = o->egt ? EG_SUSTAIN : EG_RELEASE; }
        break;
    }
    case EG_SUSTAIN: break;
    case EG_RELEASE:
        o->env += rate_units(eff_rate(o, o->rr, c));
        if (o->env >= 511) { o->env = 511; o->stage = EG_OFF; }
        break;
    default: o->env = 511;
    }
}

static double level(Op *o, Chan *c, double trem) {
    int ksl = (kslrom[c->fnum >> 6] << 2) - ((8 - c->block) << 5);
    if (ksl < 0) ksl = 0;
    double att = o->env + o->tl * 4 + (ksl >> kslshift[o->ksl]) + (o->am ? trem : 0);
    if (att >= 511) return 0;
    return pow(10.0, -att * 0.1875 / 20.0);
}

static void advance(Op *o, Chan *c, int vibpos) {
    int f = c->fnum;
    if (o->vib) {
        int range = (f >> 7) & 7;
        if (!vib_depth) range >>= 1;
        if ((vibpos & 3) == 0) range = 0;
        else if (vibpos & 1) range >>= 1;
        f += (vibpos & 4) ? -range : range;
    }
    o->phase += (uint32_t)(((uint64_t)f << c->block) * mult2[o->mult] / 2);
}

#define CYC(o) ((double)((o)->phase & 0xFFFFF) / 1048576.0)
#define PHASE10(o) ((int)(((o)->phase >> 10) & 0x3FF))

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: opl2synth events.bin out.f32\n"); return 2; }
    FILE *fi = fopen(argv[1], "rb"), *fo = fopen(argv[2], "wb");
    if (!fi || !fo) { perror("open"); return 1; }
    uint32_t total;
    if (fread(&total, 4, 1, fi) != 1) return 1;
    for (int c = 0; c < 9; c++) { opchan[chmod[c]] = c; opchan[chcar[c]] = c; }
    for (int i = 0; i < 18; i++) { op[i].env = 511; op[i].stage = EG_OFF; }

    uint8_t rec[6];
    int have = fread(rec, 6, 1, fi) == 1;
    float *buf = malloc(sizeof(float) * 4096);
    int nbuf = 0;
    for (uint32_t s = 0; s < total; s++) {
        while (have && *(uint32_t *)rec <= s) {
            write_reg(rec[4], rec[5]);
            have = fread(rec, 6, 1, fi) == 1;
        }
        double t = s / RATE;
        /* tremolo: 3.7 Hz triangle, 4.8 dB or 1 dB deep, in 0.1875 dB units */
        double trem = (1 - fabs(fmod(t * 3.7, 1.0) * 2 - 1)) * (am_depth ? 25.6 : 5.3);
        int vibpos = (int)(s >> 10) & 7;

        for (int i = 0; i < 18; i++) env_step(&op[i], &ch[opchan[i]]);
        uint32_t nbit = ((noise >> 14) ^ noise) & 1;
        noise = (noise >> 1) | (nbit << 22);

        double mix = 0;
        int rhy = rhythm_bd & 0x20;
        for (int c = 0; c < 9; c++) {
            Chan *cc = &ch[c];
            Op *m = &op[chmod[c]], *r = &op[chcar[c]];
            if (rhy && c >= 7) continue;      /* rhythm channels 7, 8 handled below */
            double fbv = cc->fb ? (m->out + m->prev) * 4.0 / (double)(1 << (9 - cc->fb)) : 0;
            double mo = wave(m->ws, CYC(m) + fbv) * level(m, cc, trem);
            m->prev = m->out; m->out = mo;
            double co;
            if (cc->cnt) co = wave(r->ws, CYC(r)) * level(r, cc, trem) + mo;
            else co = wave(r->ws, CYC(r) + mo * 4.0) * level(r, cc, trem);
            r->out = co;
            mix += (rhy && c == 6) ? co * 2 : co;
        }
        if (rhy) {
            Op *hh = &op[13], *sd = &op[16], *tom = &op[14], *tc = &op[17];
            int p13 = PHASE10(hh), p17 = PHASE10(tc);
            int b2 = (p13 >> 2) & 1, b3 = (p13 >> 3) & 1, b7 = (p13 >> 7) & 1, b8 = (p13 >> 8) & 1;
            int t3 = (p17 >> 3) & 1, t5 = (p17 >> 5) & 1;
            int x = (b2 ^ b7) | (b3 ^ t5) | (t3 ^ t5);
            int phh = (x << 9) | (((x ^ (noise & 1)) != 0) ? 0xD0 : 0x34);
            int psd = (b8 << 9) | ((b8 ^ (noise & 1)) << 8);
            int ptc = (x << 9) | 0x80;
            double ohh = wave(hh->ws, phh / 1024.0) * level(hh, &ch[7], trem);
            double osd = wave(sd->ws, psd / 1024.0) * level(sd, &ch[7], trem);
            double otom = wave(tom->ws, CYC(tom)) * level(tom, &ch[8], trem);
            double otc = wave(tc->ws, ptc / 1024.0) * level(tc, &ch[8], trem);
            mix += 2 * (ohh + osd + otom + otc);
        }
        for (int i = 0; i < 18; i++) advance(&op[i], &ch[opchan[i]], vibpos);
        buf[nbuf++] = (float)mix;
        if (nbuf == 4096) { fwrite(buf, sizeof(float), nbuf, fo); nbuf = 0; }
    }
    fwrite(buf, sizeof(float), nbuf, fo);
    fclose(fo);
    fclose(fi);
    return 0;
}
