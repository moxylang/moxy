/* moxy runtime: core (reference counts, allocation, panics) */
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MXY_THREADS
#include <pthread.h>
#endif

#define MXY_FN static __attribute__((unused))
#define MXY_INLINE static inline __attribute__((unused))
#define MXY_NORETURN __attribute__((noreturn))

/* reference counts below zero mark immortal values (literals, statics) */
MXY_INLINE void mxy_rc_inc(int64_t *rc) {
    if (__atomic_load_n(rc, __ATOMIC_RELAXED) >= 0) __atomic_fetch_add(rc, 1, __ATOMIC_RELAXED);
}

MXY_INLINE int mxy_rc_dec(int64_t *rc) {
    if (__atomic_load_n(rc, __ATOMIC_RELAXED) < 0) return 0;
    return __atomic_sub_fetch(rc, 1, __ATOMIC_ACQ_REL) == 0;
}

MXY_INLINE int64_t mxy_rc_load(int64_t *rc) { return __atomic_load_n(rc, __ATOMIC_ACQUIRE); }

MXY_FN MXY_NORETURN void mxy_panic(const char *where, const char *fmt, ...) {
    fflush(stdout);
    va_list ap;
    va_start(ap, fmt);
    fputs("panic: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (where) fprintf(stderr, "\n  at %s", where);
    fputc('\n', stderr);
    exit(101);
}

MXY_FN void *mxy_alloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) mxy_panic(NULL, "out of memory");
    return p;
}

MXY_FN void *mxy_zalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) mxy_panic(NULL, "out of memory");
    return p;
}

MXY_FN void *mxy_realloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) mxy_panic(NULL, "out of memory");
    return q;
}

MXY_INLINE uint64_t mxy_hash_u64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

MXY_INLINE uint64_t mxy_hash_bytes(const void *p, size_t n) {
    const unsigned char *s = (const unsigned char *)p;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ s[i]) * 1099511628211ull;
    return h;
}

/* growable byte buffer used for formatting */
typedef struct {
    char *data;
    int64_t len;
    int64_t cap;
} mxy_buf;

MXY_FN void mxy_buf_reserve(mxy_buf *b, int64_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    int64_t cap = b->cap ? b->cap : 64;
    while (cap < b->len + extra + 1) cap *= 2;
    b->data = (char *)mxy_realloc(b->data, (size_t)cap);
    b->cap = cap;
}

MXY_FN void mxy_buf_put(mxy_buf *b, const char *s, int64_t n) {
    mxy_buf_reserve(b, n);
    memcpy(b->data + b->len, s, (size_t)n);
    b->len += n;
    b->data[b->len] = '\0';
}

MXY_FN void mxy_buf_puts(mxy_buf *b, const char *s) { mxy_buf_put(b, s ? s : "(null)", (int64_t)strlen(s ? s : "(null)")); }

MXY_FN void mxy_buf_putc(mxy_buf *b, char c) { mxy_buf_put(b, &c, 1); }

MXY_FN void mxy_buf_printf(mxy_buf *b, const char *fmt, ...) {
    va_list ap, cp;
    va_start(ap, fmt);
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n > 0) {
        mxy_buf_reserve(b, n);
        vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
        b->len += n;
    }
    va_end(ap);
}

/* value witness: how to copy, drop, compare, hash and print one value */
typedef struct mxy_vw {
    size_t size;
    void (*copy)(void *dst, const void *src);
    void (*drop)(void *p);
    bool (*eq)(const void *a, const void *b);
    uint64_t (*hash)(const void *p);
    int (*cmp)(const void *a, const void *b);
    void (*fmt)(mxy_buf *b, const void *p, int nested);
    const char *name;
} mxy_vw;

MXY_INLINE void mxy_vw_copy(const mxy_vw *vw, void *dst, const void *src) {
    if (vw->copy) vw->copy(dst, src);
    else memcpy(dst, src, vw->size);
}

MXY_INLINE void mxy_vw_drop(const mxy_vw *vw, void *p) {
    if (vw->drop) vw->drop(p);
}

MXY_FN bool mxy_vw_eq(const mxy_vw *vw, const void *a, const void *b) {
    if (vw->eq) return vw->eq(a, b);
    return memcmp(a, b, vw->size) == 0;
}

MXY_FN uint64_t mxy_vw_hash(const mxy_vw *vw, const void *p) {
    if (vw->hash) return vw->hash(p);
    return mxy_hash_bytes(p, vw->size);
}

MXY_FN void mxy_vw_fmt(const mxy_vw *vw, mxy_buf *b, const void *p, int nested) {
    if (vw->fmt) vw->fmt(b, p, nested);
    else mxy_buf_printf(b, "<%s>", vw->name ? vw->name : "value");
}

