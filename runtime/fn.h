/* moxy runtime: closures. Captured values live after the header; `fn` takes
   the closure itself as its first argument. */
typedef struct mxy_fn {
    int64_t rc;
    void *fn;
    void (*drop)(struct mxy_fn *self);
} mxy_fn;

MXY_INLINE mxy_fn *mxy_fn_retain(mxy_fn *f) {
    if (f) mxy_rc_inc(&f->rc);
    return f;
}

MXY_FN void mxy_fn_release(mxy_fn *f) {
    if (!f || !mxy_rc_dec(&f->rc)) return;
    if (f->drop) f->drop(f);
    free(f);
}

MXY_FN MXY_NORETURN void mxy_fn_null(const char *loc) { mxy_panic(loc, "call of a null closure"); }

MXY_INLINE mxy_fn *mxy_fn_check(mxy_fn *f, const char *loc) {
    if (!f) mxy_fn_null(loc);
    return f;
}

MXY_FN void mxy_copy_fn(void *dst, const void *src) { *(mxy_fn **)dst = mxy_fn_retain(*(mxy_fn *const *)src); }
MXY_FN void mxy_drop_fn(void *p) { mxy_fn_release(*(mxy_fn **)p); }
MXY_FN void mxy_fmtp_fn(mxy_buf *b, const void *p, int nested) {
    (void)p;
    (void)nested;
    mxy_buf_puts(b, "<fn>");
}
static const mxy_vw mxy_vw_fn __attribute__((unused)) = {sizeof(mxy_fn *), mxy_copy_fn, mxy_drop_fn, mxy_eq_ptr, mxy_hash_ptr, NULL, mxy_fmtp_fn, "fn"};
