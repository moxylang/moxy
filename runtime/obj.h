/* moxy runtime: ref objects (shared, reference counted, with weak references)
   strong counts live references; weak counts weak references plus one held
   collectively by all strong references. Fields are dropped when the last
   strong reference goes; memory is freed when the last weak one goes. */
typedef struct mxy_obj {
    int64_t strong;
    int64_t weak;
    void (*drop)(void *self);
    void (*fmt)(mxy_buf *b, const void *self);
    const char *name;
} mxy_obj;

MXY_FN void *mxy_obj_new(size_t size, void (*drop)(void *), void (*fmt)(mxy_buf *, const void *), const char *name) {
    mxy_obj *o = (mxy_obj *)mxy_zalloc(size);
    o->strong = 1;
    o->weak = 1;
    o->drop = drop;
    o->fmt = fmt;
    o->name = name;
    return o;
}

MXY_INLINE void *mxy_obj_retain(void *p) {
    if (p) mxy_rc_inc(&((mxy_obj *)p)->strong);
    return p;
}

MXY_INLINE void *mxy_obj_weak_retain(void *p) {
    if (p) mxy_rc_inc(&((mxy_obj *)p)->weak);
    return p;
}

MXY_FN void mxy_obj_weak_release(void *p) {
    if (p && mxy_rc_dec(&((mxy_obj *)p)->weak)) free(p);
}

MXY_FN void mxy_obj_release(void *p) {
    mxy_obj *o = (mxy_obj *)p;
    if (!o || !mxy_rc_dec(&o->strong)) return;
    if (o->drop) o->drop(o);
    mxy_obj_weak_release(o);
}

/* strong reference from a weak one, or NULL once the object is gone */
MXY_FN void *mxy_obj_weak_load(void *p) {
    mxy_obj *o = (mxy_obj *)p;
    if (!o) return NULL;
    int64_t s = __atomic_load_n(&o->strong, __ATOMIC_ACQUIRE);
    while (s > 0)
        if (__atomic_compare_exchange_n(&o->strong, &s, s + 1, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return o;
    return NULL;
}

MXY_FN void mxy_copy_obj(void *dst, const void *src) { *(void **)dst = mxy_obj_retain(*(void *const *)src); }
MXY_FN void mxy_drop_obj(void *p) { mxy_obj_release(*(void **)p); }
MXY_FN void mxy_fmtp_obj(mxy_buf *b, const void *p, int nested) {
    (void)nested;
    const mxy_obj *o = *(mxy_obj *const *)p;
    if (!o) mxy_buf_puts(b, "null");
    else if (o->fmt) o->fmt(b, o);
    else mxy_buf_printf(b, "%s@%p", o->name ? o->name : "object", (const void *)o);
}
static const mxy_vw mxy_vw_obj __attribute__((unused)) = {sizeof(void *), mxy_copy_obj, mxy_drop_obj, mxy_eq_ptr, mxy_hash_ptr, mxy_cmp_ptr, mxy_fmtp_obj, "ref"};
