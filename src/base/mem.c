#include "base.h"

#include <stdio.h>
#include <string.h>

static void oom(void) {
    fputs("moxy: out of memory\n", stderr);
    exit(70);
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) oom();
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) oom();
    return q;
}

char *xstrdup(const char *s) {
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *xvfmt(const char *fmt, va_list ap) {
    if (!fmt) return xstrdup("");
    va_list cp;
    va_copy(cp, ap);
    char probe[1];
    int n = vsnprintf(probe, sizeof(probe), fmt, cp);
    va_end(cp);
    char *p = xmalloc((size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    return p;
}

char *xfmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *p = xvfmt(fmt, ap);
    va_end(ap);
    return p;
}
