/* moxy runtime: strings (immutable, reference counted, NUL terminated) */
typedef const char *mxy_str;

typedef struct {
    int64_t rc;
    int64_t len;
} mxy_strhdr;

#define MXY_STRHDR(s) ((mxy_strhdr *)(s) - 1)
#define MXY_LIT(name, text) static struct { mxy_strhdr h; char s[sizeof(text)]; } name __attribute__((unused)) = {{-1, (int64_t)sizeof(text) - 1}, text}

MXY_FN char *mxy_str_alloc(int64_t len) {
    mxy_strhdr *h = (mxy_strhdr *)mxy_alloc(sizeof(mxy_strhdr) + (size_t)len + 1);
    h->rc = 1;
    h->len = len;
    char *s = (char *)(h + 1);
    s[len] = '\0';
    return s;
}

MXY_FN mxy_str mxy_str_fromn(const char *p, int64_t n) {
    char *s = mxy_str_alloc(n);
    if (n) memcpy(s, p, (size_t)n);
    return s;
}

MXY_FN mxy_str mxy_str_from(const char *p) {
    if (!p) return NULL;
    return mxy_str_fromn(p, (int64_t)strlen(p));
}

/* takes ownership of the buffer's bytes */
MXY_FN mxy_str mxy_str_from_buf(mxy_buf *b) {
    mxy_str s = mxy_str_fromn(b->data ? b->data : "", b->len);
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
    return s;
}

MXY_INLINE mxy_str mxy_str_retain(mxy_str s) {
    if (s) mxy_rc_inc(&MXY_STRHDR(s)->rc);
    return s;
}

/* literals are immortal (rc < 0) and never reach free(); gcc cannot see through the atomic check */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif
MXY_INLINE void mxy_str_release(mxy_str s) {
    if (s && mxy_rc_dec(&MXY_STRHDR(s)->rc)) free(MXY_STRHDR(s));
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

MXY_INLINE int64_t mxy_str_len(mxy_str s) { return s ? MXY_STRHDR(s)->len : 0; }

MXY_FN mxy_str mxy_str_concat(mxy_str a, mxy_str b) {
    int64_t la = mxy_str_len(a), lb = mxy_str_len(b);
    char *s = mxy_str_alloc(la + lb);
    if (la) memcpy(s, a, (size_t)la);
    if (lb) memcpy(s + la, b, (size_t)lb);
    return s;
}

MXY_FN bool mxy_str_eq(mxy_str a, mxy_str b) {
    if (a == b) return true;
    if (!a || !b) return false;
    int64_t n = mxy_str_len(a);
    return n == mxy_str_len(b) && memcmp(a, b, (size_t)n) == 0;
}

MXY_FN int mxy_str_cmp(mxy_str a, mxy_str b) {
    int64_t la = mxy_str_len(a), lb = mxy_str_len(b);
    int64_t n = la < lb ? la : lb;
    int c = n ? memcmp(a, b, (size_t)n) : 0;
    if (c) return c < 0 ? -1 : 1;
    return (la > lb) - (la < lb);
}

MXY_FN char mxy_str_at(mxy_str s, int64_t i, const char *loc) {
    int64_t n = mxy_str_len(s);
    if (i < 0 || i >= n) mxy_panic(loc, "string index %lld out of range (length %lld)", (long long)i, (long long)n);
    return s[i];
}

MXY_FN mxy_str mxy_str_slice(mxy_str s, int64_t a, int64_t b, const char *loc) {
    int64_t n = mxy_str_len(s);
    if (a < 0 || b < a || b > n)
        mxy_panic(loc, "string slice [%lld, %lld) out of range (length %lld)", (long long)a, (long long)b, (long long)n);
    return mxy_str_fromn(s + a, b - a);
}

MXY_FN int64_t mxy_str_find(mxy_str s, mxy_str sub) {
    if (!s || !sub) return sub && !*sub ? 0 : -1;
    const char *p = strstr(s, sub);
    return p ? (int64_t)(p - s) : -1;
}

MXY_FN bool mxy_str_contains(mxy_str s, mxy_str sub) { return mxy_str_find(s, sub) >= 0; }

MXY_FN bool mxy_str_starts_with(mxy_str s, mxy_str p) {
    int64_t n = mxy_str_len(p);
    return n <= mxy_str_len(s) && (n == 0 || memcmp(s, p, (size_t)n) == 0);
}

MXY_FN bool mxy_str_ends_with(mxy_str s, mxy_str p) {
    int64_t n = mxy_str_len(p), m = mxy_str_len(s);
    return n <= m && (n == 0 || memcmp(s + m - n, p, (size_t)n) == 0);
}

MXY_FN mxy_str mxy_str_upper(mxy_str s) {
    int64_t n = mxy_str_len(s);
    char *r = mxy_str_alloc(n);
    for (int64_t i = 0; i < n; i++) r[i] = (char)((s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i]);
    return r;
}

MXY_FN mxy_str mxy_str_lower(mxy_str s) {
    int64_t n = mxy_str_len(s);
    char *r = mxy_str_alloc(n);
    for (int64_t i = 0; i < n; i++) r[i] = (char)((s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]);
    return r;
}

MXY_INLINE int mxy_is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

MXY_FN mxy_str mxy_str_trim(mxy_str s) {
    int64_t a = 0, b = mxy_str_len(s);
    while (a < b && mxy_is_space(s[a])) a++;
    while (b > a && mxy_is_space(s[b - 1])) b--;
    return mxy_str_fromn(s ? s + a : "", b - a);
}

MXY_FN mxy_str mxy_str_replace(mxy_str s, mxy_str from, mxy_str to) {
    int64_t n = mxy_str_len(s), fl = mxy_str_len(from), tl = mxy_str_len(to);
    if (!fl) return mxy_str_retain(s ? s : mxy_str_fromn("", 0));
    mxy_buf b = {0};
    for (int64_t i = 0; i < n;) {
        if (i + fl <= n && memcmp(s + i, from, (size_t)fl) == 0) {
            mxy_buf_put(&b, to ? to : "", tl);
            i += fl;
        } else {
            mxy_buf_putc(&b, s[i++]);
        }
    }
    return mxy_str_from_buf(&b);
}

MXY_FN mxy_str mxy_str_repeat(mxy_str s, int64_t times) {
    int64_t n = mxy_str_len(s);
    if (times < 0) times = 0;
    char *r = mxy_str_alloc(n * times);
    for (int64_t i = 0; i < times; i++) memcpy(r + i * n, s, (size_t)n);
    return r;
}

MXY_FN bool mxy_str_parse_long(mxy_str s, long *out) {
    if (!s || !*s) return false;
    char *end;
    long v = strtol(s, &end, 10);
    while (*end && mxy_is_space(*end)) end++;
    if (*end) return false;
    *out = v;
    return true;
}

MXY_FN bool mxy_str_parse_double(mxy_str s, double *out) {
    if (!s || !*s) return false;
    char *end;
    double v = strtod(s, &end);
    while (*end && mxy_is_space(*end)) end++;
    if (*end) return false;
    *out = v;
    return true;
}
