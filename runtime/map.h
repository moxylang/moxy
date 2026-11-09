/* moxy runtime: maps (insertion-ordered hash maps, copy-on-write; NULL is empty) */
typedef struct {
    uint64_t hash;
    int64_t live;
} mxy_entry;

typedef struct mxy_map {
    int64_t rc;
    int64_t len;    /* live entries */
    int64_t count;  /* used entry slots (live + removed) */
    int64_t cap;    /* entry slots */
    const mxy_vw *kvw;
    const mxy_vw *vvw;
    size_t koff;
    size_t voff;
    size_t esize;
    char *entries;
    int64_t *index; /* -1 empty, -2 removed, otherwise entry slot */
    int64_t icap;
} mxy_map;

#define MXY_ALIGN8(n) (((n) + 7) & ~(size_t)7)
#define MXY_ENTRY(m, i) ((mxy_entry *)((m)->entries + (size_t)(i) * (m)->esize))
#define MXY_KEY(m, i) ((void *)((m)->entries + (size_t)(i) * (m)->esize + (m)->koff))
#define MXY_VAL(m, i) ((void *)((m)->entries + (size_t)(i) * (m)->esize + (m)->voff))

MXY_FN mxy_map *mxy_map_new(const mxy_vw *kvw, const mxy_vw *vvw, int64_t cap) {
    mxy_map *m = (mxy_map *)mxy_zalloc(sizeof(mxy_map));
    m->rc = 1;
    m->kvw = kvw;
    m->vvw = vvw;
    m->koff = sizeof(mxy_entry);
    m->voff = m->koff + MXY_ALIGN8(kvw->size);
    m->esize = m->voff + MXY_ALIGN8(vvw->size);
    m->cap = cap < 8 ? 8 : cap;
    m->entries = (char *)mxy_alloc((size_t)m->cap * m->esize);
    m->icap = 16;
    while (m->icap < m->cap * 2) m->icap *= 2;
    m->index = (int64_t *)mxy_alloc((size_t)m->icap * sizeof(int64_t));
    for (int64_t i = 0; i < m->icap; i++) m->index[i] = -1;
    return m;
}

MXY_INLINE mxy_map *mxy_map_retain(mxy_map *m) {
    if (m) mxy_rc_inc(&m->rc);
    return m;
}

MXY_FN void mxy_map_release(mxy_map *m) {
    if (!m || !mxy_rc_dec(&m->rc)) return;
    for (int64_t i = 0; i < m->count; i++) {
        if (!MXY_ENTRY(m, i)->live) continue;
        mxy_vw_drop(m->kvw, MXY_KEY(m, i));
        mxy_vw_drop(m->vvw, MXY_VAL(m, i));
    }
    free(m->entries);
    free(m->index);
    free(m);
}

MXY_INLINE int64_t mxy_map_len(const mxy_map *m) { return m ? m->len : 0; }

MXY_FN int64_t mxy_map_find(const mxy_map *m, const void *key, uint64_t h) {
    if (!m) return -1;
    int64_t mask = m->icap - 1;
    for (int64_t i = (int64_t)(h & (uint64_t)mask);; i = (i + 1) & mask) {
        int64_t slot = m->index[i];
        if (slot == -1) return -1;
        if (slot >= 0 && MXY_ENTRY(m, slot)->hash == h && mxy_vw_eq(m->kvw, MXY_KEY(m, slot), key)) return slot;
    }
}

MXY_FN void mxy_map_reindex(mxy_map *m, int64_t icap) {
    free(m->index);
    m->icap = icap;
    m->index = (int64_t *)mxy_alloc((size_t)icap * sizeof(int64_t));
    for (int64_t i = 0; i < icap; i++) m->index[i] = -1;
    for (int64_t s = 0; s < m->count; s++) {
        if (!MXY_ENTRY(m, s)->live) continue;
        int64_t i = (int64_t)(MXY_ENTRY(m, s)->hash & (uint64_t)(icap - 1));
        while (m->index[i] != -1) i = (i + 1) & (icap - 1);
        m->index[i] = s;
    }
}

/* drop removed slots and grow so one more entry fits */
MXY_FN void mxy_map_grow(mxy_map *m) {
    if (m->count < m->cap) return;
    int64_t w = 0;
    for (int64_t s = 0; s < m->count; s++) {
        if (!MXY_ENTRY(m, s)->live) continue;
        if (w != s) memcpy(m->entries + (size_t)w * m->esize, m->entries + (size_t)s * m->esize, m->esize);
        w++;
    }
    m->count = w;
    if (m->count * 2 >= m->cap) {
        m->cap *= 2;
        m->entries = (char *)mxy_realloc(m->entries, (size_t)m->cap * m->esize);
    }
    int64_t icap = m->icap;
    while (icap < m->cap * 2) icap *= 2;
    mxy_map_reindex(m, icap);
}

MXY_FN mxy_map *mxy_map_clone(const mxy_map *src) {
    mxy_map *m = mxy_map_new(src->kvw, src->vvw, src->len);
    for (int64_t s = 0; s < src->count; s++) {
        if (!MXY_ENTRY(src, s)->live) continue;
        int64_t d = m->count++;
        *MXY_ENTRY(m, d) = *MXY_ENTRY(src, s);
        mxy_vw_copy(m->kvw, MXY_KEY(m, d), MXY_KEY(src, s));
        mxy_vw_copy(m->vvw, MXY_VAL(m, d), MXY_VAL(src, s));
        m->len++;
    }
    mxy_map_reindex(m, m->icap);
    return m;
}

MXY_FN void mxy_map_unique(mxy_map **mp, const mxy_vw *kvw, const mxy_vw *vvw) {
    if (!*mp) {
        *mp = mxy_map_new(kvw, vvw, 8);
        return;
    }
    if (mxy_rc_load(&(*mp)->rc) == 1) return;
    mxy_map *c = mxy_map_clone(*mp);
    mxy_map_release(*mp);
    *mp = c;
}

MXY_FN void mxy_map_set(mxy_map **mp, const mxy_vw *kvw, const mxy_vw *vvw, const void *key, const void *val,
                        int kmove, int vmove) {
    mxy_map_unique(mp, kvw, vvw);
    mxy_map *m = *mp;
    uint64_t h = mxy_vw_hash(kvw, key);
    int64_t slot = mxy_map_find(m, key, h);
    if (slot >= 0) {
        if (kmove) mxy_vw_drop(kvw, (void *)key);
        mxy_vw_drop(vvw, MXY_VAL(m, slot));
        if (vmove) memcpy(MXY_VAL(m, slot), val, vvw->size);
        else mxy_vw_copy(vvw, MXY_VAL(m, slot), val);
        return;
    }
    mxy_map_grow(m);
    slot = m->count++;
    MXY_ENTRY(m, slot)->hash = h;
    MXY_ENTRY(m, slot)->live = 1;
    if (kmove) memcpy(MXY_KEY(m, slot), key, kvw->size);
    else mxy_vw_copy(kvw, MXY_KEY(m, slot), key);
    if (vmove) memcpy(MXY_VAL(m, slot), val, vvw->size);
    else mxy_vw_copy(vvw, MXY_VAL(m, slot), val);
    m->len++;
    int64_t i = (int64_t)(h & (uint64_t)(m->icap - 1));
    while (m->index[i] >= 0) i = (i + 1) & (m->icap - 1);
    m->index[i] = slot;
}

MXY_FN void *mxy_map_get(const mxy_map *m, const mxy_vw *kvw, const void *key, const char *loc) {
    int64_t slot = mxy_map_find(m, key, mxy_vw_hash(kvw, key));
    if (slot < 0) {
        mxy_buf b = {0};
        mxy_vw_fmt(kvw, &b, key, 1);
        mxy_panic(loc, "key %s not found in map", b.data ? b.data : "");
    }
    return MXY_VAL(m, slot);
}

MXY_FN void *mxy_map_lookup(const mxy_map *m, const mxy_vw *kvw, const void *key) {
    int64_t slot = mxy_map_find(m, key, mxy_vw_hash(kvw, key));
    return slot < 0 ? NULL : MXY_VAL(m, slot);
}

MXY_FN bool mxy_map_has(const mxy_map *m, const mxy_vw *kvw, const void *key) {
    return mxy_map_find(m, key, mxy_vw_hash(kvw, key)) >= 0;
}

MXY_FN bool mxy_map_remove(mxy_map **mp, const mxy_vw *kvw, const mxy_vw *vvw, const void *key) {
    if (!*mp || !mxy_map_has(*mp, kvw, key)) return false;
    mxy_map_unique(mp, kvw, vvw);
    mxy_map *m = *mp;
    uint64_t h = mxy_vw_hash(kvw, key);
    int64_t mask = m->icap - 1;
    for (int64_t i = (int64_t)(h & (uint64_t)mask);; i = (i + 1) & mask) {
        int64_t slot = m->index[i];
        if (slot == -1) return false;
        if (slot >= 0 && MXY_ENTRY(m, slot)->hash == h && mxy_vw_eq(kvw, MXY_KEY(m, slot), key)) {
            mxy_vw_drop(kvw, MXY_KEY(m, slot));
            mxy_vw_drop(vvw, MXY_VAL(m, slot));
            MXY_ENTRY(m, slot)->live = 0;
            m->index[i] = -2;
            m->len--;
            return true;
        }
    }
}

MXY_FN void mxy_map_clear(mxy_map **mp) {
    mxy_map_release(*mp);
    *mp = NULL;
}

MXY_FN mxy_list *mxy_map_keys(const mxy_map *m, const mxy_vw *kvw) {
    mxy_list *l = mxy_list_new(kvw, mxy_map_len(m));
    for (int64_t s = 0; m && s < m->count; s++)
        if (MXY_ENTRY(m, s)->live) mxy_list_push(&l, kvw, MXY_KEY(m, s), 0);
    return l;
}

MXY_FN mxy_list *mxy_map_values(const mxy_map *m, const mxy_vw *vvw) {
    mxy_list *l = mxy_list_new(vvw, mxy_map_len(m));
    for (int64_t s = 0; m && s < m->count; s++)
        if (MXY_ENTRY(m, s)->live) mxy_list_push(&l, vvw, MXY_VAL(m, s), 0);
    return l;
}

MXY_INLINE int64_t mxy_map_slots(const mxy_map *m) { return m ? m->count : 0; }
MXY_INLINE bool mxy_map_live(const mxy_map *m, int64_t s) { return MXY_ENTRY(m, s)->live != 0; }

MXY_FN bool mxy_map_eq(const mxy_map *a, const mxy_map *b) {
    if (mxy_map_len(a) != mxy_map_len(b)) return false;
    for (int64_t s = 0; a && s < a->count; s++) {
        if (!MXY_ENTRY(a, s)->live) continue;
        void *v = mxy_map_lookup(b, a->kvw, MXY_KEY(a, s));
        if (!v || !mxy_vw_eq(a->vvw, MXY_VAL(a, s), v)) return false;
    }
    return true;
}

MXY_FN void mxy_map_fmt(mxy_buf *b, const mxy_map *m) {
    mxy_buf_putc(b, '{');
    int first = 1;
    for (int64_t s = 0; m && s < m->count; s++) {
        if (!MXY_ENTRY(m, s)->live) continue;
        if (!first) mxy_buf_puts(b, ", ");
        first = 0;
        mxy_vw_fmt(m->kvw, b, MXY_KEY(m, s), 1);
        mxy_buf_puts(b, ": ");
        mxy_vw_fmt(m->vvw, b, MXY_VAL(m, s), 1);
    }
    mxy_buf_putc(b, '}');
}

MXY_FN void mxy_copy_map(void *dst, const void *src) { *(mxy_map **)dst = mxy_map_retain(*(mxy_map *const *)src); }
MXY_FN void mxy_drop_map(void *p) { mxy_map_release(*(mxy_map **)p); }
MXY_FN bool mxy_eq_map(const void *a, const void *b) { return mxy_map_eq(*(mxy_map *const *)a, *(mxy_map *const *)b); }
MXY_FN void mxy_fmtp_map(mxy_buf *b, const void *p, int nested) {
    (void)nested;
    mxy_map_fmt(b, *(mxy_map *const *)p);
}
static const mxy_vw mxy_vw_map __attribute__((unused)) = {sizeof(mxy_map *), mxy_copy_map, mxy_drop_map, mxy_eq_map, NULL, NULL, mxy_fmtp_map, "map"};

MXY_FN void *mxy_map_get_mut(mxy_map **mp, const mxy_vw *kvw, const mxy_vw *vvw, const void *key, const char *loc) {
    if (!mxy_map_has(*mp, kvw, key)) mxy_map_get(*mp, kvw, key, loc);
    mxy_map_unique(mp, kvw, vvw);
    return mxy_map_get(*mp, kvw, key, loc);
}
