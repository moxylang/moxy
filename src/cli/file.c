#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "diag/diag.h"
#include "load/load.h"

int project_flags_for(const char *dir, char **cflags, char **ldflags, StrList *includes, StrList *sources);

typedef struct {
    int release;
    int sanitize;
    int lines;
    const char *output;
    const char *file;
    int rest;
} FileArgs;

static int parse_args(int argc, char **argv, FileArgs *a, int stop_at_file) {
    memset(a, 0, sizeof(*a));
    a->lines = 1;
    for (int i = 0; i < argc; i++) {
        const char *s = argv[i];
        if (!a->file && (strcmp(s, "--release") == 0 || strcmp(s, "-r") == 0)) a->release = 1;
        else if (!a->file && strcmp(s, "--sanitize") == 0) a->sanitize = 1;
        else if (!a->file && strcmp(s, "--no-lines") == 0) a->lines = 0;
        else if (!a->file && strcmp(s, "-o") == 0 && i + 1 < argc) a->output = argv[++i];
        else if (!a->file && str_starts(s, "--enable-")) continue; /* accepted for compatibility */
        else if (!a->file && str_starts(s, "-I") && s[2]) load_add_include_dir(s + 2);
        else if (!a->file) {
            a->file = s;
            a->rest = i + 1;
            if (stop_at_file) return 0;
        } else if (strcmp(s, "-o") == 0 && i + 1 < argc) {
            a->output = argv[++i];
        } else if (strcmp(s, "--release") == 0 || strcmp(s, "-r") == 0) {
            a->release = 1;
        }
    }
    return a->file ? 0 : -1;
}

static char *stem_of(const char *path) {
    const char *base = path_base(path);
    const char *dot = strrchr(base, '.');
    return dot ? xstrndup(base, (size_t)(dot - base)) : xstrdup(base);
}

/* compile `file` into a binary at `out`; returns 0 on success */
static int build_binary(const FileArgs *a, const char *out, const char *cpath) {
    char *dir = path_dir(a->file);
    load_add_include_dir(dir);
    CompileOpts co = {a->lines, 0, cpath};
    int threads = 0, has_main = 0;
    char *c = compile_to_c(a->file, &co, &threads, &has_main);
    if (!c) return 1;
    if (!has_main) {
        fprintf(stderr, "moxy: %s has no main function\n", a->file);
        return 1;
    }
    if (file_write(cpath, c) != 0) {
        fprintf(stderr, "moxy: cannot write %s\n", cpath);
        return 1;
    }
    CcOpts cc = {0};
    char *cflags = NULL, *ldflags = NULL;
    project_flags_for(dir, &cflags, &ldflags, &cc.includes, &cc.sources);
    source_dirs(&cc.includes);
    cc.cflags = cflags;
    cc.ldflags = ldflags;
    cc.output = out;
    cc.release = a->release;
    cc.sanitize = a->sanitize;
    cc.threads = threads;
    vec_push(cc.sources, xstrdup(cpath));
    return cc_run(&cc);
}

int cmd_emit(int argc, char **argv) {
    FileArgs a;
    if (parse_args(argc, argv, &a, 0) != 0) {
        fprintf(stderr, "usage: moxy emit <file.mxy> [-o out.c] [--no-lines]\n");
        return 1;
    }
    load_add_include_dir(path_dir(a.file));
    CompileOpts co = {a.lines, 0, a.output ? a.output : "moxy.c"};
    char *c = compile_to_c(a.file, &co, NULL, NULL);
    if (!c) return 1;
    if (a.output) return file_write(a.output, c) == 0 ? 0 : 1;
    fputs(c, stdout);
    return 0;
}

int cmd_run_file(int argc, char **argv) {
    FileArgs a;
    if (parse_args(argc, argv, &a, 1) != 0) {
        fprintf(stderr, "usage: moxy run [--release] [--sanitize] <file.mxy> [args...]\n");
        return 1;
    }
    char *tmp = make_temp_dir();
    if (!tmp) {
        fprintf(stderr, "moxy: cannot create a temporary directory\n");
        return 1;
    }
    char *stem = stem_of(a.file);
    char *cpath = xfmt("%s/%s.c", tmp, stem);
    char *bin = xfmt("%s/%s", tmp, stem);
    int rc = build_binary(&a, bin, cpath);
    if (rc == 0) rc = run_binary(bin, argc - a.rest, argv + a.rest);
    remove_tree(tmp);
    return rc;
}

int cmd_build_file(int argc, char **argv) {
    FileArgs a;
    if (parse_args(argc, argv, &a, 0) != 0) {
        fprintf(stderr, "usage: moxy build <file.mxy> [-o output] [--release]\n");
        return 1;
    }
    char *stem = stem_of(a.file);
    const char *out = a.output ? a.output : stem;
    char *tmp = make_temp_dir();
    char *cpath = xfmt("%s/%s.c", tmp ? tmp : ".", stem);
    int rc = build_binary(&a, out, cpath);
    if (tmp) remove_tree(tmp);
    return rc;
}

int cmd_check(int argc, char **argv) {
    StrList files = {0};
    for (int i = 0; i < argc; i++) {
        if (str_ends(argv[i], ".mxy")) vec_push(files, argv[i]);
        else collect_mxy(argv[i], &files);
    }
    if (!argc) collect_mxy(".", &files);
    int failed = 0;
    for (int i = 0; i < files.len; i++) {
        load_add_include_dir(path_dir(files.items[i]));
        if (check_file(files.items[i], 0)) failed++;
    }
    if (failed) fprintf(stderr, "%d file%s with errors\n", failed, failed == 1 ? "" : "s");
    return failed ? 1 : 0;
}
