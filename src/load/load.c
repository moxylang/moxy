#include "load.h"

#include <string.h>
#include <unistd.h>

#include "diag/diag.h"
#include "embed/embed.h"
#include "lex/lexer.h"

typedef Vec(char *) StrVec;

static StrVec include_dirs;
static StrVec loaded;

void load_add_include_dir(const char *dir) {
    vec_push(include_dirs, xstrdup(dir));
}

void load_reset(void) {
    for (int i = 0; i < loaded.len; i++) free(loaded.items[i]);
    loaded.len = 0;
}

static int seen(const char *key) {
    for (int i = 0; i < loaded.len; i++)
        if (strcmp(loaded.items[i], key) == 0) return 1;
    vec_push(loaded, xstrdup(key));
    return 0;
}

static char *display_path(const char *real) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        size_t n = strlen(cwd);
        if (strncmp(real, cwd, n) == 0 && real[n] == '/') return xstrdup(real + n + 1);
    }
    return xstrdup(real);
}

/* parse `#include "x"` or `#include <x>`; returns target or NULL */
static char *include_target(const char *text, int *angle) {
    const char *p = text + 1;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "include", 7) != 0) return NULL;
    p += 7;
    while (*p == ' ' || *p == '\t') p++;
    char close = *p == '"' ? '"' : *p == '<' ? '>' : 0;
    if (!close) return NULL;
    const char *end = strchr(p + 1, close);
    if (!end) return NULL;
    *angle = close == '>';
    return xstrndup(p + 1, (size_t)(end - p - 1));
}

static void load_into(TokenVec *out, int file, const char *dir);

static int resolve(const char *target, const char *dir, char **real, const char **embedded) {
    *real = NULL;
    *embedded = NULL;
    char *cand = path_join(dir, target);
    if (file_exists(cand)) {
        *real = path_real(cand);
        free(cand);
        return 1;
    }
    free(cand);
    for (int i = 0; i < include_dirs.len; i++) {
        cand = path_join(include_dirs.items[i], target);
        if (file_exists(cand)) {
            *real = path_real(cand);
            free(cand);
            return 1;
        }
        free(cand);
    }
    *embedded = embed_std(target);
    return *embedded != NULL;
}

static void splice(TokenVec *out, Token *dir_tok, const char *target, const char *dir) {
    char *real;
    const char *embedded;
    if (!resolve(target, dir, &real, &embedded)) {
        diag_error(dir_tok->loc, "cannot find '%s'", target);
        diag_hint("looked next to the including file, in include paths, and in the standard library");
        return;
    }
    char *key = real ? xstrdup(real) : xfmt("<std>/%s", target);
    if (seen(key)) {
        free(key);
        free(real);
        return;
    }
    free(key);
    char *text;
    int file;
    char *subdir;
    if (real) {
        text = file_read(real);
        if (!text) {
            diag_error(dir_tok->loc, "cannot read '%s'", real);
            free(real);
            return;
        }
        char *disp = display_path(real);
        file = diag_add_file(disp, disp, text);
        free(disp);
        subdir = path_dir(real);
        free(real);
    } else {
        text = xstrdup(embedded);
        file = diag_add_file(target, target, text);
        subdir = xstrdup(".");
    }
    load_into(out, file, subdir);
    free(subdir);
}

static void load_into(TokenVec *out, int file, const char *dir) {
    const SrcFile *f = diag_file(file);
    TokenVec toks = lex_file(file, f->text);
    for (int i = 0; i < toks.len; i++) {
        Token *t = &toks.items[i];
        if (t->kind == T_EOF) break;
        if (t->kind == T_DIRECTIVE) {
            int angle = 0;
            char *target = include_target(t->text, &angle);
            if (target && str_ends(target, ".mxy")) {
                splice(out, t, target, dir);
                free(target);
                continue;
            }
            free(target);
        }
        vec_push(*out, *t);
    }
    free(toks.items);
}

static TokenVec finish(TokenVec out, int file) {
    Token eof = {T_EOF, xstrdup(""), {file, 0, 0, 0}, 1};
    if (out.len > 0) eof.loc = out.items[out.len - 1].loc;
    vec_push(out, eof);
    return out;
}

TokenVec load_program(const char *path) {
    TokenVec out = {0};
    char *text = file_read(path);
    if (!text) {
        diag_error((Loc){-1, 0, 0, 0}, "cannot read '%s'", path);
        diag_bail();
    }
    char *real = path_real(path);
    seen(real);
    char *disp = display_path(real);
    int file = diag_add_file(disp, disp, text);
    char *dir = path_dir(real);
    load_into(&out, file, dir);
    free(dir);
    free(disp);
    free(real);
    return finish(out, file);
}

TokenVec load_source(const char *display, const char *src) {
    TokenVec out = {0};
    int file = diag_add_file(display, display, xstrdup(src));
    char *dir = path_dir(display);
    load_into(&out, file, dir);
    free(dir);
    return finish(out, file);
}
