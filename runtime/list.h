/* moxy runtime: lists (copy-on-write, reference counted; NULL is the empty list) */
typedef struct mxy_list {
    int64_t rc;
    int64_t len;
    int64_t cap;
    const mxy_vw *vw;
    char *data;
} mxy_list;

#define MXY_AT(l, i) ((l)->data + (size_t)(i) * (l)->vw->size)

MXY_FN mxy_list *mxy_list_new(const mxy_vw *vw, int64_t cap) {
    mxy_list *l = (mxy_list *)mxy_alloc(sizeof(mxy_list));
    l->rc = 1;
    l->len = 0;
    l->cap = cap < 4 ? 4 : cap;
    l->vw = vw;
    l->data = (char *)mxy_alloc((size_t)l->cap * vw->size);
    return l;
}

MXY_INLINE mxy_list *mxy_list_retain(mxy_list *l) {
    if (l) mxy_rc_inc(&l->rc);
    return l;
}

MXY_FN void mxy_list_release(mxy_list *l) {
    if (!l || !mxy_rc_dec(&l->rc)) return;
    if (l->vw->drop)
        for (int64_t i = 0; i < l->len; i++) l->vw->drop(MXY_AT(l, i));
    free(l->data);
    free(l);
}

MXY_INLINE int64_t mxy_list_len(const mxy_list *l) { return l ? l->len : 0; }

/* takes ownership of n elements at `elems` */
MXY_FN mxy_list *mxy_list_from(const mxy_vw *vw, int64_t n, const void *elems) {
    mxy_list *l = mxy_list_new(vw, n);
    if (n) memcpy(l->data, elems, (size_t)n * vw->size);
    l->len = n;
    return l;
}

MXY_FN mxy_list *mxy_list_clone(const mxy_list *src, const mxy_vw *vw, int64_t cap) {
    mxy_list *l = mxy_list_new(vw, cap);
    int64_t n = mxy_list_len(src);
    for (int64_t i = 0; i < n; i++) mxy_vw_copy(vw, MXY_AT(l, i), MXY_AT(src, i));
    l->len = n;
    return l;
}

/* make *lp uniquely owned before mutation */
MXY_FN void mxy_list_unique(mxy_list **lp, const mxy_vw *vw) {
    mxy_list *l = *lp;
    if (!l) {
        *lp = mxy_list_new(vw, 4);
        return;
    }
    if (l->rc == 1 || l->rc < 0) return;
    mxy_list *c = mxy_list_clone(l, l->vw, l->cap);
    mxy_list_release(l);
    *lp = c;
}

MXY_FN void mxy_list_reserve(mxy_list *l, int64_t n) {
    if (n <= l->cap) return;
    int64_t cap = l->cap * 2;
    if (cap < n) cap = n;
    l->data = (char *)mxy_realloc(l->data, (size_t)cap * l->vw->size);
    l->cap = cap;
}

MXY_FN void mxy_list_push(mxy_list **lp, const mxy_vw *vw, const void *elem, int move) {
    mxy_list_unique(lp, vw);
    mxy_list *l = *lp;
    mxy_list_reserve(l, l->len + 1);
    if (move) memcpy(MXY_AT(l, l->len), elem, vw->size);
    else mxy_vw_copy(vw, MXY_AT(l, l->len), elem);
    l->len++;
}

MXY_FN MXY_NORETURN void mxy_list_oob(int64_t i, int64_t n, const char *loc) {
    mxy_panic(loc, "index %lld out of range for list of length %lld", (long long)i, (long long)n);
}

MXY_INLINE void *mxy_list_at(const mxy_list *l, int64_t i, const char *loc) {
    int64_t n = mxy_list_len(l);
    if (i < 0 || i >= n) mxy_list_oob(i, n, loc);
    return MXY_AT(l, i);
}

MXY_FN void *mxy_list_at_mut(mxy_list **lp, const mxy_vw *vw, int64_t i, const char *loc) {
    int64_t n = mxy_list_len(*lp);
    if (i < 0 || i >= n) mxy_list_oob(i, n, loc);
    mxy_list_unique(lp, vw);
    return MXY_AT(*lp, i);
}

MXY_FN void mxy_list_set(mxy_list **lp, const mxy_vw *vw, int64_t i, const void *val, int move, const char *loc) {
    void *slot = mxy_list_at_mut(lp, vw, i, loc);
    char old[vw->size > 0 ? vw->size : 1];
    memcpy(old, slot, vw->size);
    if (move) memcpy(slot, val, vw->size);
    else mxy_vw_copy(vw, slot, val);
    mxy_vw_drop(vw, old);
}

MXY_FN void mxy_list_pop(mxy_list **lp, const mxy_vw *vw, void *out, const char *loc) {
    if (!mxy_list_len(*lp)) mxy_panic(loc, "pop from an empty list");
    mxy_list_unique(lp, vw);
    mxy_list *l = *lp;
    l->len--;
    memcpy(out, MXY_AT(l, l->len), vw->size);
}

MXY_FN void mxy_list_insert(mxy_list **lp, const mxy_vw *vw, int64_t i, const void *val, int move, const char *loc) {
    int64_t n = mxy_list_len(*lp);
    if (i < 0 || i > n) mxy_list_oob(i, n, loc);
    mxy_list_unique(lp, vw);
    mxy_list *l = *lp;
    mxy_list_reserve(l, l->len + 1);
    memmove(MXY_AT(l, i + 1), MXY_AT(l, i), (size_t)(l->len - i) * vw->size);
    if (move) memcpy(MXY_AT(l, i), val, vw->size);
    else mxy_vw_copy(vw, MXY_AT(l, i), val);
    l->len++;
}

MXY_FN void mxy_list_remove(mxy_list **lp, const mxy_vw *vw, int64_t i, void *out, const char *loc) {
    int64_t n = mxy_list_len(*lp);
    if (i < 0 || i >= n) mxy_list_oob(i, n, loc);
    mxy_list_unique(lp, vw);
    mxy_list *l = *lp;
    memcpy(out, MXY_AT(l, i), vw->size);
    memmove(MXY_AT(l, i), MXY_AT(l, i + 1), (size_t)(l->len - i - 1) * vw->size);
    l->len--;
}

MXY_FN void mxy_list_clear(mxy_list **lp) {
    mxy_list_release(*lp);
    *lp = NULL;
}

MXY_FN int64_t mxy_list_index_of(const mxy_list *l, const mxy_vw *vw, const void *x) {
    for (int64_t i = 0; i < mxy_list_len(l); i++)
        if (mxy_vw_eq(vw, MXY_AT(l, i), x)) return i;
    return -1;
}

MXY_FN mxy_list *mxy_list_slice(const mxy_list *l, const mxy_vw *vw, int64_t a, int64_t b, const char *loc) {
    int64_t n = mxy_list_len(l);
    if (a < 0 || b < a || b > n)
        mxy_panic(loc, "slice [%lld, %lld) out of range for list of length %lld", (long long)a, (long long)b, (long long)n);
    mxy_list *r = mxy_list_new(vw, b - a);
    for (int64_t i = a; i < b; i++) mxy_vw_copy(vw, MXY_AT(r, i - a), MXY_AT(l, i));
    r->len = b - a;
    return r;
}

MXY_FN mxy_list *mxy_list_concat(const mxy_list *a, const mxy_list *b, const mxy_vw *vw) {
    mxy_list *r = mxy_list_clone(a, vw, mxy_list_len(a) + mxy_list_len(b));
    for (int64_t i = 0; i < mxy_list_len(b); i++) mxy_vw_copy(vw, MXY_AT(r, r->len++), MXY_AT(b, i));
    return r;
}

MXY_FN void mxy_list_reverse(mxy_list **lp, const mxy_vw *vw) {
    if (mxy_list_len(*lp) < 2) return;
    mxy_list_unique(lp, vw);
    mxy_list *l = *lp;
    char tmp[vw->size];
    for (int64_t i = 0, j = l->len - 1; i < j; i++, j--) {
        memcpy(tmp, MXY_AT(l, i), vw->size);
        memcpy(MXY_AT(l, i), MXY_AT(l, j), vw->size);
        memcpy(MXY_AT(l, j), tmp, vw->size);
    }
}

MXY_FN void mxy_merge_sort(char *a, char *tmp, int64_t n, const mxy_vw *vw) {
    if (n < 2) return;
    int64_t mid = n / 2;
    size_t sz = vw->size;
    mxy_merge_sort(a, tmp, mid, vw);
    mxy_merge_sort(a + (size_t)mid * sz, tmp, n - mid, vw);
    int64_t i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        if (vw->cmp(a + (size_t)j * sz, a + (size_t)i * sz) < 0) memcpy(tmp + (size_t)k++ * sz, a + (size_t)j++ * sz, sz);
        else memcpy(tmp + (size_t)k++ * sz, a + (size_t)i++ * sz, sz);
    }
    while (i < mid) memcpy(tmp + (size_t)k++ * sz, a + (size_t)i++ * sz, sz);
    while (j < n) memcpy(tmp + (size_t)k++ * sz, a + (size_t)j++ * sz, sz);
    memcpy(a, tmp, (size_t)n * sz);
}

MXY_FN void mxy_list_sort(mxy_list **lp, const mxy_vw *vw) {
    if (mxy_list_len(*lp) < 2) return;
    if (!vw->cmp) mxy_panic(NULL, "values of type %s cannot be sorted", vw->name);
    mxy_list_unique(lp, vw);
    mxy_list *l = *lp;
    char *tmp = (char *)mxy_alloc((size_t)l->len * vw->size);
    mxy_merge_sort(l->data, tmp, l->len, vw);
    free(tmp);
}

MXY_FN bool mxy_list_eq(const mxy_list *a, const mxy_list *b) {
    int64_t n = mxy_list_len(a);
    if (n != mxy_list_len(b)) return false;
    for (int64_t i = 0; i < n; i++)
        if (!mxy_vw_eq(a->vw, MXY_AT(a, i), MXY_AT(b, i))) return false;
    return true;
}

MXY_FN void mxy_list_fmt(mxy_buf *b, const mxy_list *l) {
    mxy_buf_putc(b, '[');
    for (int64_t i = 0; i < mxy_list_len(l); i++) {
        if (i) mxy_buf_puts(b, ", ");
        mxy_vw_fmt(l->vw, b, MXY_AT(l, i), 1);
    }
    mxy_buf_putc(b, ']');
}

MXY_FN mxy_str mxy_list_join(const mxy_list *l, mxy_str sep) {
    mxy_buf b = {0};
    for (int64_t i = 0; i < mxy_list_len(l); i++) {
        if (i && sep) mxy_buf_put(&b, sep, mxy_str_len(sep));
        mxy_vw_fmt(l->vw, &b, MXY_AT(l, i), 0);
    }
    return mxy_str_from_buf(&b);
}

MXY_FN void mxy_copy_list(void *dst, const void *src) { *(mxy_list **)dst = mxy_list_retain(*(mxy_list *const *)src); }
MXY_FN void mxy_drop_list(void *p) { mxy_list_release(*(mxy_list **)p); }
MXY_FN bool mxy_eq_list(const void *a, const void *b) { return mxy_list_eq(*(mxy_list *const *)a, *(mxy_list *const *)b); }
MXY_FN uint64_t mxy_hash_list(const void *p) {
    const mxy_list *l = *(mxy_list *const *)p;
    uint64_t h = 7;
    for (int64_t i = 0; i < mxy_list_len(l); i++) h = h * 31 + mxy_vw_hash(l->vw, MXY_AT(l, i));
    return h;
}
MXY_FN void mxy_fmtp_list(mxy_buf *b, const void *p, int nested) {
    (void)nested;
    mxy_list_fmt(b, *(mxy_list *const *)p);
}
static const mxy_vw mxy_vw_list __attribute__((unused)) = {sizeof(mxy_list *), mxy_copy_list, mxy_drop_list, mxy_eq_list, mxy_hash_list, NULL, mxy_fmtp_list, "list"};

MXY_FN mxy_list *mxy_str_split(mxy_str s, mxy_str sep) {
    mxy_list *out = mxy_list_new(&mxy_vw_str, 4);
    int64_t n = mxy_str_len(s), m = mxy_str_len(sep);
    int64_t start = 0;
    if (m == 0) {
        for (int64_t i = 0; i < n; i++) {
            mxy_str c = mxy_str_fromn(s + i, 1);
            mxy_list_push(&out, &mxy_vw_str, &c, 1);
        }
        return out;
    }
    for (int64_t i = 0; i + m <= n;) {
        if (memcmp(s + i, sep, (size_t)m) == 0) {
            mxy_str part = mxy_str_fromn(s + start, i - start);
            mxy_list_push(&out, &mxy_vw_str, &part, 1);
            i += m;
            start = i;
        } else {
            i++;
        }
    }
    mxy_str last = mxy_str_fromn(s ? s + start : "", n - start);
    mxy_list_push(&out, &mxy_vw_str, &last, 1);
    return out;
}

MXY_FN mxy_list *mxy_str_lines(mxy_str s) {
    mxy_list *out = mxy_list_new(&mxy_vw_str, 4);
    int64_t n = mxy_str_len(s), start = 0;
    for (int64_t i = 0; i < n; i++) {
        if (s[i] != '\n') continue;
        int64_t end = i > start && s[i - 1] == '\r' ? i - 1 : i;
        mxy_str part = mxy_str_fromn(s + start, end - start);
        mxy_list_push(&out, &mxy_vw_str, &part, 1);
        start = i + 1;
    }
    if (start < n) {
        mxy_str part = mxy_str_fromn(s + start, n - start);
        mxy_list_push(&out, &mxy_vw_str, &part, 1);
    }
    return out;
}

MXY_FN mxy_list *mxy_str_chars(mxy_str s) {
    int64_t n = mxy_str_len(s);
    return mxy_list_from(&mxy_vw_char, n, s ? s : "");
}
