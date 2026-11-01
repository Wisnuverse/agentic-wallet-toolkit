#define _POSIX_C_SOURCE 199309L
// tailsolve.c — multithreaded (pthread) Keccak-256 hash-tail solver.
// Finds a decimal nonce such that keccak256(prefix + nonce) starts with
// at least BITS zero bits. Same semantics as Boltbook's tail.mjs.
//
// Usage:
//   tailsolve "PREFIX" BITS [THREADS] [START_NONCE]
//   tailsolve --selftest
// Output (one JSON line): {"nonce":"...","hash":"...","tried":N,"seconds":S,"threads":T}
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

/* ---------------- Keccak-f[1600] with generated constants ---------------- */
static uint64_t RC_[24];
static unsigned ROT_[25];
static int PILN_[25];

static uint64_t rotl64(uint64_t x, unsigned n) {
    n &= 63;
    return n ? (x << n) | (x >> (64 - n)) : x;
}

static void gen_tables(void) {
    /* Round constants via the Keccak LFSR (poly x^8+x^6+x^5+x^4+1) */
    uint8_t R = 0x01;
    for (int i = 0; i < 24; i++) {
        RC_[i] = 0;
        for (int j = 0; j < 7; j++) {
            if (R & 1) RC_[i] |= (uint64_t)1 << ((1 << j) - 1);
            if (R & 0x80) R = (uint8_t)((R << 1) ^ 0x71);
            else          R = (uint8_t)(R << 1);
        }
    }
    /* Rho offsets: lane trajectory starting at (1,0), r = (t+1)(t+2)/2 mod 64 */
    int x = 1, y = 0;
    ROT_[0] = 0;
    for (int t = 0; t < 24; t++) {
        ROT_[x + 5 * y] = (unsigned)(((t + 1) * (t + 2) / 2) % 64);
        int nx = y, ny = (2 * x + 3 * y) % 5;
        x = nx; y = ny;
    }
    /* Pi: lane (x,y) moves to (y, (2x+3y) mod 5) */
    for (int xx = 0; xx < 5; xx++)
        for (int yy = 0; yy < 5; yy++)
            PILN_[xx + 5 * yy] = yy + 5 * ((2 * xx + 3 * yy) % 5);
}

static void keccakf(uint64_t st[25]) {
    uint64_t bc[5], t[25];
    for (int round = 0; round < 24; round++) {
        for (int i = 0; i < 5; i++)
            bc[i] = st[i] ^ st[i+5] ^ st[i+10] ^ st[i+15] ^ st[i+20];
        for (int i = 0; i < 5; i++) {
            uint64_t d = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) st[j + i] ^= d;
        }
        for (int i = 0; i < 25; i++) t[PILN_[i]] = rotl64(st[i], ROT_[i]);
        for (int j = 0; j < 25; j += 5) {
            uint64_t r0=t[j], r1=t[j+1], r2=t[j+2], r3=t[j+3], r4=t[j+4];
            st[j]   = r0 ^ ((~r1) & r2);
            st[j+1] = r1 ^ ((~r2) & r3);
            st[j+2] = r2 ^ ((~r3) & r4);
            st[j+3] = r3 ^ ((~r4) & r0);
            st[j+4] = r4 ^ ((~r0) & r1);
        }
        st[0] ^= RC_[round];
    }
}

/* ---------------- Keccak-256 sponge (rate 136, suffix 0x01) ---------------- */
#define RATE 136
static void absorb_block(uint64_t st[25], const uint8_t *blk) {
    for (int i = 0; i < RATE / 8; i++) {
        uint64_t v;
        memcpy(&v, blk + 8 * i, 8);
        st[i] ^= v; /* little-endian host (x86_64) */
    }
}

static void keccak256(const uint8_t *in, size_t inlen, uint8_t out[32]) {
    uint64_t st[25];
    memset(st, 0, sizeof st);
    while (inlen >= RATE) {
        absorb_block(st, in);
        keccakf(st);
        in += RATE; inlen -= RATE;
    }
    uint8_t blk[RATE];
    memset(blk, 0, RATE);
    memcpy(blk, in, inlen);
    blk[inlen] = 0x01;
    blk[RATE - 1] |= 0x80;
    absorb_block(st, blk);
    keccakf(st);
    for (int i = 0; i < 4; i++) memcpy(out + 8 * i, &st[i], 8);
}

static int leading_zero_bits(const uint8_t h[32]) {
    int n = 0;
    for (int i = 0; i < 32; i++) {
        if (h[i] == 0) { n += 8; continue; }
        n += __builtin_clz(h[i]) - 24;
        break;
    }
    return n;
}

/* ---------------- Solver ---------------- */

/* ---------------- Pixel dig miner ----------------
   msg = seed(32) || nonce(32); nonce = prefix(24) || counter u32LE @24 || hi u32LE @28
   hit iff (hash[i] & mask[i]) == target[i] for all i in 0..7 (MSB-first pixel bits) */
static uint8_t g_seed[32], g_target[8], g_mask[8];
static atomic_int g_found, g_active;
static atomic_ullong g_tried;
static uint8_t g_hit_nonce[32], g_hit_hash[32];
static double g_deadline; /* epoch seconds, 0 = none */

static double now_s(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static double wall_s(void) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct { int idx; uint8_t prefix[24]; uint32_t hi; } warg_t;
static void *worker(void *arg) {
    warg_t *w = (warg_t *)arg;
    uint8_t msg[64], h[32];
    memcpy(msg, g_seed, 32);
    memcpy(msg + 32, w->prefix, 24);
    uint32_t hi = w->hi, counter = 0;
    unsigned long long local = 0;
    for (;;) {
        if (atomic_load_explicit(&g_found, memory_order_relaxed)) break;
        if (g_deadline && wall_s() > g_deadline) break;
        memcpy(msg + 32 + 24, &counter, 4);
        memcpy(msg + 32 + 28, &hi, 4);
        keccak256(msg, 64, h);
        local++;
        if ((h[0] & g_mask[0]) == g_target[0] && (h[1] & g_mask[1]) == g_target[1] &&
            (h[2] & g_mask[2]) == g_target[2] && (h[3] & g_mask[3]) == g_target[3] &&
            (h[4] & g_mask[4]) == g_target[4] && (h[5] & g_mask[5]) == g_target[5] &&
            (h[6] & g_mask[6]) == g_target[6] && (h[7] & g_mask[7]) == g_target[7]) {
            int expected = 0;
            if (atomic_compare_exchange_strong(&g_found, &expected, 1)) {
                memcpy(g_hit_nonce, msg + 32, 32);
                memcpy(g_hit_hash, h, 32);
            }
            atomic_fetch_add_explicit(&g_tried, local, memory_order_relaxed);
            break;
        }
        counter++;
        if (counter == 0) { hi++; memcpy(msg + 32 + 28, &hi, 4); }
        if ((local & 0xFFFF) == 0) {
            atomic_fetch_add_explicit(&g_tried, local, memory_order_relaxed);
            local = 0;
            if (atomic_load_explicit(&g_found, memory_order_relaxed)) break;
            if (g_deadline && wall_s() > g_deadline) break;
        }
    }
    atomic_fetch_sub_explicit(&g_active, 1, memory_order_relaxed);
    return NULL;
}

static int hexval(char c) { return (c <= '9') ? c - '0' : (c <= 'f') ? c - 'a' + 10 : c - 'A' + 10; }
static void unhex(const char *s, uint8_t *out, int n) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (int i = 0; i < n; i++) out[i] = (uint8_t)(hexval(s[2*i]) * 16 + hexval(s[2*i+1]));
}
static void tohex(const uint8_t *in, int n, char *out) {
    for (int i = 0; i < n; i++) sprintf(out + 2*i, "%02x", in[i]);
}
static void set_target(const uint8_t icon[8], int d) {
    memset(g_mask, 0, 8);
    for (int i = 0; i < d; i++) g_mask[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    for (int i = 0; i < 8; i++) g_target[i] = icon[i] & g_mask[i];
}

int main(int argc, char **argv) {
    gen_tables();
    if (argc >= 2 && strcmp(argv[1], "--verify") == 0) {
        /* --verify SEED ICON DIFF PREFIX_HEX HI FROM COUNT : JS scan() semantics, single thread */
        uint8_t icon[8], prefix[24];
        unhex(argv[2], g_seed, 32); unhex(argv[3], icon, 8);
        set_target(icon, atoi(argv[4]));
        unhex(argv[5], prefix, 24);
        uint32_t hi = (uint32_t)strtoul(argv[6], NULL, 10);
        uint32_t from = (uint32_t)strtoul(argv[7], NULL, 10);
        uint32_t count = (uint32_t)strtoul(argv[8], NULL, 10);
        uint8_t msg[64], h[32];
        memcpy(msg, g_seed, 32); memcpy(msg + 32, prefix, 24); memcpy(msg + 32 + 28, &hi, 4);
        long found = -1;
        for (uint32_t c = from; c < from + count; c++) {
            memcpy(msg + 32 + 24, &c, 4);
            keccak256(msg, 64, h);
            int ok = 1;
            for (int i = 0; i < 8; i++) if ((h[i] & g_mask[i]) != g_target[i]) { ok = 0; break; }
            if (ok) { found = (long)c; break; }
        }
        char nh[65], hh[65]; tohex(msg + 32, 32, nh); tohex(h, 32, hh);
        printf("{\"counter\":%ld,\"nonce_hex\":\"%s\",\"hash_hex\":\"%s\"}\n", found, nh, hh);
        return 0;
    }
    if (argc < 8) {
        fprintf(stderr, "usage: pixmine SEED ICON DIFF THREADS MAX_SECONDS RESULT_FILE PROGRESS_FILE\n");
        return 2;
    }
    uint8_t icon[8];
    unhex(argv[1], g_seed, 32); unhex(argv[2], icon, 8);
    int diff = atoi(argv[3]);
    set_target(icon, diff);
    long nthreads = atol(argv[4]);
    double maxsec = atof(argv[5]);
    const char *result_file = argv[6], *progress_file = argv[7];
    if (nthreads < 1) nthreads = 1; if (nthreads > 64) nthreads = 64;
    g_deadline = maxsec > 0 ? wall_s() + maxsec : 0;
    atomic_init(&g_found, 0); atomic_init(&g_tried, 0); atomic_init(&g_active, (int)nthreads);
    /* random prefixes/hi per thread */
    FILE *ur = fopen("/dev/urandom", "rb");
    warg_t *args = calloc(nthreads, sizeof(warg_t));
    pthread_t *th = calloc(nthreads, sizeof(pthread_t));
    double t0 = now_s();
    for (long i = 0; i < nthreads; i++) {
        args[i].idx = (int)i;
        if (ur) { if (fread(args[i].prefix, 1, 24, ur) != 24 || fread(&args[i].hi, 1, 4, ur) != 4) {} }
        args[i].prefix[23] ^= (uint8_t)i;
        pthread_create(&th[i], NULL, worker, &args[i]);
    }
    if (ur) fclose(ur);
    while (atomic_load(&g_active) > 0 && !atomic_load(&g_found)) {
        struct timespec ts = {5, 0}; nanosleep(&ts, NULL);
        double el = now_s() - t0;
        unsigned long long tr = atomic_load(&g_tried);
        FILE *pf = fopen(progress_file, "w");
        if (pf) { fprintf(pf, "{\"tried\":%llu,\"seconds\":%.1f,\"hashrate\":%lld,\"found\":%d}\n", tr, el, (long long)(el > 0 ? tr / el : 0), atomic_load(&g_found)); fclose(pf); }
    }
    for (long i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double el = now_s() - t0;
    unsigned long long tr = atomic_load(&g_tried);
    if (atomic_load(&g_found)) {
        char nh[65], hh[65]; tohex(g_hit_nonce, 32, nh); tohex(g_hit_hash, 32, hh);
        FILE *rf = fopen(result_file, "w");
        if (rf) { fprintf(rf, "{\"nonce_hex\":\"%s\",\"hash_hex\":\"%s\",\"tried\":%llu,\"seconds\":%.1f}\n", nh, hh, tr, el); fclose(rf); }
        printf("{\"found\":true,\"nonce_hex\":\"%s\",\"hash_hex\":\"%s\",\"tried\":%llu,\"seconds\":%.1f,\"hashrate\":%lld}\n", nh, hh, tr, el, (long long)(el > 0 ? tr / el : 0));
    } else {
        printf("{\"found\":false,\"tried\":%llu,\"seconds\":%.1f,\"hashrate\":%lld}\n", tr, el, (long long)(el > 0 ? tr / el : 0));
    }
    return 0;
}
