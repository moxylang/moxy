#include "map.h"

#include <string.h>

static unsigned long hash(const char *s) {
    unsigned long h = 1469598103934665603ul;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ul;
    return h;
}

static void grow(StrMap *m) {
    StrMap old = *m;
    m->cap = old.cap ? old.cap * 2 : 16;
    m->keys = xcalloc((size_t)m->cap, sizeof(char *));
    m->vals = xcalloc((size_t)m->cap, sizeof(void *));
    m->len = 0;
    for (int i = 0; i < old.cap; i++)
        if (old.keys[i]) strmap_put(m, old.keys[i], old.vals[i]);
    for (int i = 0; i < old.cap; i++) free(old.keys[i]);
    free(old.keys);
    free(old.vals);
}

void *strmap_get(const StrMap *m, const char *key) {
    if (!m->cap) return NULL;
    unsigned long i = hash(key) & (unsigned long)(m->cap - 1);
    while (m->keys[i]) {
        if (strcmp(m->keys[i], key) == 0) return m->vals[i];
        i = (i + 1) & (unsigned long)(m->cap - 1);
    }
    return NULL;
}

void strmap_put(StrMap *m, const char *key, void *val) {
    if ((m->len + 1) * 2 > m->cap) grow(m);
    unsigned long i = hash(key) & (unsigned long)(m->cap - 1);
    while (m->keys[i]) {
        if (strcmp(m->keys[i], key) == 0) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (unsigned long)(m->cap - 1);
    }
    m->keys[i] = xstrdup(key);
    m->vals[i] = val;
    m->len++;
}

void strmap_free(StrMap *m) {
    for (int i = 0; i < m->cap; i++) free(m->keys[i]);
    free(m->keys);
    free(m->vals);
    memset(m, 0, sizeof(*m));
}
