#define _DEFAULT_SOURCE
#include "conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

MoxyConfig mxyconf_defaults(void) {
    MoxyConfig c;
    c.indent = 4;
    c.brace_knr = 1;
    c.space_around_ops = 1;
    c.space_after_comma = 1;
    c.space_after_keyword = 1;
    c.trailing_newline = 1;
    c.max_line_length = 0;
    c.lint_unused_vars = 1;
    c.lint_empty_blocks = 1;
    c.lint_shadow_vars = 1;
    return c;
}

static void trim(char *s) {
    char *end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

static int parse_bool(const char *val) {
    if (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0 || strcmp(val, "1") == 0)
        return 1;
    return 0;
}

MoxyConfig mxyconf_load(const char *path) {
    MoxyConfig cfg = mxyconf_defaults();
    FILE *f = fopen(path, "r");
    if (!f) return cfg;

    char line[512];
    int section = 0; /* 0=none, 1=format, 2=lint */

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        if (strcmp(line, "format:") == 0) { section = 1; continue; }
        if (strcmp(line, "lint:") == 0) { section = 2; continue; }

        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        char key[128], val[128];
        strncpy(key, line, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        strncpy(val, colon + 1, sizeof(val) - 1);
        val[sizeof(val) - 1] = '\0';
        trim(key);
        trim(val);

        if (section == 1) {
            if (strcmp(key, "indent") == 0) cfg.indent = atoi(val);
            else if (strcmp(key, "brace_style") == 0) cfg.brace_knr = (strcmp(val, "knr") == 0) ? 1 : 0;
            else if (strcmp(key, "space_around_ops") == 0) cfg.space_around_ops = parse_bool(val);
            else if (strcmp(key, "space_after_comma") == 0) cfg.space_after_comma = parse_bool(val);
            else if (strcmp(key, "space_after_keyword") == 0) cfg.space_after_keyword = parse_bool(val);
            else if (strcmp(key, "trailing_newline") == 0) cfg.trailing_newline = parse_bool(val);
            else if (strcmp(key, "max_line_length") == 0) cfg.max_line_length = atoi(val);
        } else if (section == 2) {
            if (strcmp(key, "unused_vars") == 0) cfg.lint_unused_vars = parse_bool(val);
            else if (strcmp(key, "empty_blocks") == 0) cfg.lint_empty_blocks = parse_bool(val);
            else if (strcmp(key, "shadow_vars") == 0) cfg.lint_shadow_vars = parse_bool(val);
        }
    }

    fclose(f);
    return cfg;
}

char *mxyconf_find(const char *start_dir, const char *file_dir) {
    const char *name = "moxyfmt.yaml";
    char path[1024];

    if (start_dir) {
        snprintf(path, sizeof(path), "%s/%s", start_dir, name);
        FILE *f = fopen(path, "r");
        if (f) { fclose(f); return strdup(path); }
    }

    if (file_dir && (!start_dir || strcmp(file_dir, start_dir) != 0)) {
        snprintf(path, sizeof(path), "%s/%s", file_dir, name);
        FILE *f = fopen(path, "r");
        if (f) { fclose(f); return strdup(path); }
    }

    return NULL;
}
