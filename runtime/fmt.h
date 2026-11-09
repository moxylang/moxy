/* moxy runtime: formatting and value witnesses for builtin types */
MXY_FN void mxy_fmt_double(mxy_buf *b, double v) {
    char tmp[64];
    if (v != v) {
        mxy_buf_puts(b, "nan");
        return;
    }
    if (v == 1.0 / 0.0 || v == -1.0 / 0.0) {
        mxy_buf_puts(b, v > 0 ? "inf" : "-inf");
        return;
    }
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(tmp, sizeof(tmp), "%.*g", prec, v);
        if (strtod(tmp, NULL) == v) break;
    }
    mxy_buf_puts(b, tmp);
    if (!strpbrk(tmp, ".eni")) mxy_buf_puts(b, ".0");
}

MXY_FN void mxy_fmt_float(mxy_buf *b, float v) {
    char tmp[64];
    if (v != v) {
        mxy_buf_puts(b, "nan");
        return;
    }
    for (int prec = 1; prec <= 9; prec++) {
        snprintf(tmp, sizeof(tmp), "%.*g", prec, (double)v);
        if (strtof(tmp, NULL) == v) break;
    }
    mxy_buf_puts(b, tmp);
    if (!strpbrk(tmp, ".eni")) mxy_buf_puts(b, ".0");
}

MXY_FN void mxy_fmt_quoted(mxy_buf *b, const char *s, int64_t n, char q) {
    mxy_buf_putc(b, q);
    for (int64_t i = 0; i < n; i++) {
        char c = s[i];
        switch (c) {
        case '\n': mxy_buf_puts(b, "\\n"); break;
        case '\t': mxy_buf_puts(b, "\\t"); break;
        case '\r': mxy_buf_puts(b, "\\r"); break;
        case '\\': mxy_buf_puts(b, "\\\\"); break;
        default:
            if (c == q) mxy_buf_putc(b, '\\');
            mxy_buf_putc(b, c);
        }
    }
    mxy_buf_putc(b, q);
}

/* typed formatters (by value) used by print and interpolation */
MXY_FN void mxy_fmtv_bool(mxy_buf *b, int v) { mxy_buf_puts(b, v ? "true" : "false"); }
MXY_FN void mxy_fmtv_char(mxy_buf *b, char v) { mxy_buf_putc(b, v); }
MXY_FN void mxy_fmtv_schar(mxy_buf *b, signed char v) { mxy_buf_printf(b, "%d", (int)v); }
MXY_FN void mxy_fmtv_uchar(mxy_buf *b, unsigned char v) { mxy_buf_printf(b, "%u", (unsigned)v); }
MXY_FN void mxy_fmtv_short(mxy_buf *b, short v) { mxy_buf_printf(b, "%d", (int)v); }
MXY_FN void mxy_fmtv_ushort(mxy_buf *b, unsigned short v) { mxy_buf_printf(b, "%u", (unsigned)v); }
MXY_FN void mxy_fmtv_int(mxy_buf *b, int v) { mxy_buf_printf(b, "%d", v); }
MXY_FN void mxy_fmtv_uint(mxy_buf *b, unsigned v) { mxy_buf_printf(b, "%u", v); }
MXY_FN void mxy_fmtv_long(mxy_buf *b, long v) { mxy_buf_printf(b, "%ld", v); }
MXY_FN void mxy_fmtv_ulong(mxy_buf *b, unsigned long v) { mxy_buf_printf(b, "%lu", v); }
MXY_FN void mxy_fmtv_llong(mxy_buf *b, long long v) { mxy_buf_printf(b, "%lld", v); }
MXY_FN void mxy_fmtv_ullong(mxy_buf *b, unsigned long long v) { mxy_buf_printf(b, "%llu", v); }
MXY_FN void mxy_fmtv_float(mxy_buf *b, float v) { mxy_fmt_float(b, v); }
MXY_FN void mxy_fmtv_double(mxy_buf *b, double v) { mxy_fmt_double(b, v); }
MXY_FN void mxy_fmtv_ldouble(mxy_buf *b, long double v) { mxy_fmt_double(b, (double)v); }
MXY_FN void mxy_fmtv_cstr(mxy_buf *b, const char *v) { mxy_buf_puts(b, v); }
MXY_FN void mxy_fmtv_str(mxy_buf *b, mxy_str v) { if (v) mxy_buf_put(b, v, mxy_str_len(v)); else mxy_buf_puts(b, "null"); }
MXY_FN void mxy_fmtv_ptr(mxy_buf *b, const void *v) {
    if (v) mxy_buf_printf(b, "%p", v);
    else mxy_buf_puts(b, "null");
}

/* print values of C types Moxy cannot see, chosen by the C compiler */
#define MXY_FMT_ANY(b, x) _Generic((x),                          \
    _Bool: mxy_fmtv_bool, char: mxy_fmtv_char,                    \
    signed char: mxy_fmtv_schar, unsigned char: mxy_fmtv_uchar,   \
    short: mxy_fmtv_short, unsigned short: mxy_fmtv_ushort,       \
    int: mxy_fmtv_int, unsigned int: mxy_fmtv_uint,               \
    long: mxy_fmtv_long, unsigned long: mxy_fmtv_ulong,           \
    long long: mxy_fmtv_llong, unsigned long long: mxy_fmtv_ullong, \
    float: mxy_fmtv_float, double: mxy_fmtv_double,               \
    long double: mxy_fmtv_ldouble, char *: mxy_fmtv_cstr,          \
    const char *: mxy_fmtv_cstr, default: mxy_fmtv_ptr)(b, x)

MXY_FN void mxy_buf_flush(mxy_buf *b, FILE *f) {
    if (b->len) fwrite(b->data, 1, (size_t)b->len, f);
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

#define MXY_PRIM_VW(NAME, T, FMTEXPR)                                                     \
    MXY_FN bool mxy_eq_##NAME(const void *a, const void *b) { return *(const T *)a == *(const T *)b; } \
    MXY_FN uint64_t mxy_hash_##NAME(const void *p) { return mxy_hash_u64((uint64_t) * (const T *)p); } \
    MXY_FN int mxy_cmp_##NAME(const void *a, const void *b) {                              \
        T x = *(const T *)a, y = *(const T *)b;                                             \
        return (x > y) - (x < y);                                                          \
    }                                                                                      \
    MXY_FN void mxy_fmtp_##NAME(mxy_buf *b, const void *p, int nested) {                   \
        (void)nested;                                                                      \
        T v = *(const T *)p;                                                               \
        FMTEXPR;                                                                           \
    }                                                                                      \
    static const mxy_vw mxy_vw_##NAME __attribute__((unused)) = {                          \
        sizeof(T), NULL, NULL, mxy_eq_##NAME, mxy_hash_##NAME, mxy_cmp_##NAME, mxy_fmtp_##NAME, #NAME};

MXY_PRIM_VW(bool, bool, mxy_fmtv_bool(b, v))
MXY_PRIM_VW(char, char, if (nested) mxy_fmt_quoted(b, &v, 1, '\''); else mxy_buf_putc(b, v))
MXY_PRIM_VW(schar, signed char, mxy_fmtv_schar(b, v))
MXY_PRIM_VW(uchar, unsigned char, mxy_fmtv_uchar(b, v))
MXY_PRIM_VW(short, short, mxy_fmtv_short(b, v))
MXY_PRIM_VW(ushort, unsigned short, mxy_fmtv_ushort(b, v))
MXY_PRIM_VW(int, int, mxy_fmtv_int(b, v))
MXY_PRIM_VW(uint, unsigned int, mxy_fmtv_uint(b, v))
MXY_PRIM_VW(long, long, mxy_fmtv_long(b, v))
MXY_PRIM_VW(ulong, unsigned long, mxy_fmtv_ulong(b, v))
MXY_PRIM_VW(llong, long long, mxy_fmtv_llong(b, v))
MXY_PRIM_VW(ullong, unsigned long long, mxy_fmtv_ullong(b, v))
MXY_PRIM_VW(float, float, mxy_fmtv_float(b, v))
MXY_PRIM_VW(double, double, mxy_fmtv_double(b, v))
MXY_PRIM_VW(ldouble, long double, mxy_fmtv_ldouble(b, v))

MXY_FN bool mxy_eq_ptr(const void *a, const void *b) { return *(void *const *)a == *(void *const *)b; }
MXY_FN uint64_t mxy_hash_ptr(const void *p) { return mxy_hash_u64((uint64_t)(uintptr_t) * (void *const *)p); }
MXY_FN int mxy_cmp_ptr(const void *a, const void *b) {
    uintptr_t x = (uintptr_t) * (void *const *)a, y = (uintptr_t) * (void *const *)b;
    return (x > y) - (x < y);
}
MXY_FN void mxy_fmtp_ptr(mxy_buf *b, const void *p, int nested) {
    (void)nested;
    mxy_fmtv_ptr(b, *(void *const *)p);
}
static const mxy_vw mxy_vw_ptr __attribute__((unused)) = {sizeof(void *), NULL, NULL, mxy_eq_ptr, mxy_hash_ptr, mxy_cmp_ptr, mxy_fmtp_ptr, "pointer"};

MXY_FN void mxy_copy_str(void *dst, const void *src) { *(mxy_str *)dst = mxy_str_retain(*(const mxy_str *)src); }
MXY_FN void mxy_drop_str(void *p) { mxy_str_release(*(mxy_str *)p); }
MXY_FN bool mxy_eq_str(const void *a, const void *b) { return mxy_str_eq(*(const mxy_str *)a, *(const mxy_str *)b); }
MXY_FN uint64_t mxy_hash_str(const void *p) {
    mxy_str s = *(const mxy_str *)p;
    return mxy_hash_bytes(s ? s : "", (size_t)mxy_str_len(s));
}
MXY_FN int mxy_cmp_str(const void *a, const void *b) { return mxy_str_cmp(*(const mxy_str *)a, *(const mxy_str *)b); }
MXY_FN void mxy_fmtp_str(mxy_buf *b, const void *p, int nested) {
    mxy_str s = *(const mxy_str *)p;
    if (nested && s) mxy_fmt_quoted(b, s, mxy_str_len(s), '"');
    else mxy_fmtv_str(b, s);
}
static const mxy_vw mxy_vw_str __attribute__((unused)) = {sizeof(mxy_str), mxy_copy_str, mxy_drop_str, mxy_eq_str, mxy_hash_str, mxy_cmp_str, mxy_fmtp_str, "string"};

MXY_FN mxy_str mxy_str_of(const mxy_vw *vw, const void *p) {
    mxy_buf b = {0};
    mxy_vw_fmt(vw, &b, p, 0);
    return mxy_str_from_buf(&b);
}
