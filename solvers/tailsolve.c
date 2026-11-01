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
static const char *g_prefix;
static size_t g_plen;
static int g_bits;
static atomic_int g_found;
static atomic_ullong g_tried;
static uint64_t g_result_nonce;
static uint8_t g_result_hash[32];

typedef struct { uint64_t start, stride; } warg_t;
static void *worker2(void *arg) {
    warg_t *w = (warg_t *)arg;
    uint8_t msg[512];
    memcpy(msg, g_prefix, g_plen);
    uint64_t nonce = w->start;
    uint8_t h[32];
    char digits[24];
    while (!atomic_load_explicit(&g_found, memory_order_relaxed)) {
        int dlen = snprintf(digits, sizeof digits, "%llu", (unsigned long long)nonce);
        memcpy(msg + g_plen, digits, dlen);
        keccak256(msg, g_plen + (size_t)dlen, h);
        atomic_fetch_add_explicit(&g_tried, 1, memory_order_relaxed);
        if (leading_zero_bits(h) >= g_bits) {
            int expected = 0;
            if (atomic_compare_exchange_strong(&g_found, &expected, 1)) {
                g_result_nonce = nonce;
                memcpy(g_result_hash, h, 32);
            }
            break;
        }
        nonce += w->stride;
        if ((nonce & 0x3FFFFF) == 0 && atomic_load(&g_found)) break;
    }
    return NULL;
}

static double now_s(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int selftest(void) {
    uint8_t h[32];
    char hex[65];
    struct { const char *in; const char *want; } v[3] = {
        {"", "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"},
        {"abc", "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45"},
        {"The quick brown fox jumps over the lazy dog",
         "4d741b6f1eb29cb2a9b9911c82f56fa8d73b04959d3d9d222895df6c0b28aa15"},
    };
    for (int i = 0; i < 3; i++) {
        keccak256((const uint8_t *)v[i].in, strlen(v[i].in), h);
        for (int j = 0; j < 32; j++) sprintf(hex + 2 * j, "%02x", h[j]);
        if (strcmp(hex, v[i].want) != 0) {
            printf("SELFTEST FAIL case %d: got %s want %s\n", i, hex, v[i].want);
            return 1;
        }
    }
    /* long input (>1 block) cross-checked externally against js-sha3 */
    printf("SELFTEST OK (empty, abc, fox vectors match Keccak-256)\n");
    return 0;
}

int main(int argc, char **argv) {
    gen_tables();
    if (argc >= 2 && strcmp(argv[1], "--selftest") == 0) return selftest();
    if (argc >= 3 && strcmp(argv[1], "--hash") == 0) {
        uint8_t h[32]; keccak256((const uint8_t *)argv[2], strlen(argv[2]), h);
        for (int j = 0; j < 32; j++) printf("%02x", h[j]);
        printf("\n");
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: tailsolve \"PREFIX\" BITS [THREADS] [START]\n");
        return 2;
    }
    g_prefix = argv[1];
    g_plen = strlen(g_prefix);
    g_bits = atoi(argv[2]);
    long nthreads = (argc >= 4) ? atol(argv[3]) : 2;
    uint64_t start = (argc >= 5) ? strtoull(argv[4], NULL, 10) : 0;
    if (nthreads < 1) nthreads = 1;
    if (nthreads > 64) nthreads = 64;

    pthread_t th[64];
    warg_t args[64];
    atomic_init(&g_found, 0);
    atomic_init(&g_tried, 0);
    double t0 = now_s();
    for (long i = 0; i < nthreads; i++) {
        args[i].start = start + (uint64_t)i;
        args[i].stride = (uint64_t)nthreads;
        pthread_create(&th[i], NULL, worker2, &args[i]);
    }
    for (long i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double dt = now_s() - t0;
    char hex[65];
    for (int j = 0; j < 32; j++) sprintf(hex + 2 * j, "%02x", g_result_hash[j]);
    printf("{\"nonce\":\"%llu\",\"hash\":\"%s\",\"tried\":%llu,\"seconds\":%.3f,\"threads\":%ld,\"bits\":%d}\n",
           (unsigned long long)g_result_nonce, hex,
           (unsigned long long)atomic_load(&g_tried), dt, nthreads, g_bits);
    return 0;
}
