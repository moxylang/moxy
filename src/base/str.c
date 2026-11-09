#include "base.h"

#include <string.h>

int str_ends(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && memcmp(s + n - m, suffix, m) == 0;
}

int str_starts(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

/* Levenshtein distance, capped for long inputs */
int str_distance(const char *a, const char *b) {
    int n = (int)strlen(a), m = (int)strlen(b);
    if (n > 64 || m > 64) return 99;
    int row[65];
    for (int j = 0; j <= m; j++) row[j] = j;
    for (int i = 1; i <= n; i++) {
        int prev = row[0];
        row[0] = i;
        for (int j = 1; j <= m; j++) {
            int cur = row[j];
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int best = prev + cost;
            if (row[j] + 1 < best) best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = best;
            prev = cur;
        }
    }
    return row[m];
}
