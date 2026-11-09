#ifndef MOXY_MAP_H
#define MOXY_MAP_H

#include "base.h"

/* open-addressing string -> pointer map */
typedef struct {
    char **keys;
    void **vals;
    int cap;
    int len;
} StrMap;

void *strmap_get(const StrMap *m, const char *key);
void strmap_put(StrMap *m, const char *key, void *val);
void strmap_free(StrMap *m);

#endif
