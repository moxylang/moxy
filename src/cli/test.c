#define _DEFAULT_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cli.h"
#include "load/load.h"

int project_flags_for(const char *dir, char **cflags, char **ldflags, StrList *includes, StrList *sources);

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int skip_dir(const char *name) {
    return name[0] == '.' || strcmp(name, "build") == 0 || strcmp(name, "packages") == 0 ||
           strcmp(name, "node_modules") == 0;
}

static int is_test_file(const char *path, int in_tests) {
    if (!str_ends(path, ".mxy")) return 0;
    return in_tests || str_ends(path, ".test.mxy") || str_ends(path, "_test.mxy");
}

static void walk(const char *dir, int in_tests, StrList *out) {
    DIR *d = opendir(dir);
    if (!d) return;
    StrList names = {0};
    struct dirent *e;
    while ((e = readdir(d))) vec_push(names, xstrdup(e->d_name));
    closedir(d);
    for (int i = 0; i < names.len; i++)
        for (int j = i + 1; j < names.len; j++)
            if (strcmp(names.items[i], names.items[j]) > 0) {
                char *t = names.items[i];
                names.items[i] = names.items[j];
                names.items[j] = t;
            }
    for (int i = 0; i < names.len; i++) {
        const char *name = names.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char *path = strcmp(dir, ".") == 0 ? xstrdup(name) : path_join(dir, name);
        if (dir_exists(path)) {
            if (!skip_dir(name)) walk(path, in_tests || strcmp(name, "tests") == 0, out);
        } else if (is_test_file(path, in_tests)) {
            vec_push(*out, path);
        }
    }
}

static int run_capture(const char *bin, const char *out_path, int sanitize) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
#ifdef __linux__
        /* leak checking for the program under test (the compiler opts out) */
        if (sanitize) setenv("ASAN_OPTIONS", "detect_leaks=1", 1);
#else
        (void)sanitize;
#endif
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            close(fd);
        }
        execl(bin, bin, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) return 1;
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

static char *expected_path(const char *test) {
    size_t n = strlen(test);
    return xfmt("%.*s.out", (int)(n - 4), test);
}

static char *errors_path(const char *test) {
    size_t n = strlen(test);
    return xfmt("%.*s.err", (int)(n - 4), test);
}

/* compiles with stderr captured into `log`; returns the generated C or NULL */
static char *compile_captured(const char *path, CompileOpts *co, int *threads, int *has_main, const char *log) {
    fflush(stderr);
    int saved = dup(2);
    int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        dup2(fd, 2);
        close(fd);
    }
    char *c = compile_to_c(path, co, threads, has_main);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    return c;
}

/* a test with a .err file must fail to compile with each listed message */
static int expect_failure(const char *path, const char *errfile, const char *log, char **why) {
    CompileOpts co = {0, 0, "x.c"};
    char *c = compile_captured(path, &co, NULL, NULL, log);
    char *got = file_read(log);
    char *want = file_read(errfile);
    if (c) {
        *why = xstrdup("compiled, but errors were expected");
        return 0;
    }
    char *line = want;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (*line && (!got || !strstr(got, line))) {
            *why = xfmt("missing diagnostic: %s", line);
            if (got) fputs(got, stderr);
            return 0;
        }
        line = nl ? nl + 1 : NULL;
    }
    return 1;
}

typedef enum { T_PASS, T_FAIL, T_SKIP } Outcome;

static Outcome run_one(const char *path, int sanitize, int release, char **why) {
    *why = NULL;
    char *tmp = make_temp_dir();
    if (!tmp) {
        *why = xstrdup("cannot create a temporary directory");
        return T_FAIL;
    }
    char *cpath = xfmt("%s/test.c", tmp);
    char *bin = xfmt("%s/test", tmp);
    char *out = xfmt("%s/stdout", tmp);
    char *dir = path_dir(path);
    load_add_include_dir(dir);
    char *errfile = errors_path(path);
    if (file_exists(errfile)) {
        char *log = xfmt("%s/stderr", tmp);
        Outcome r = expect_failure(path, errfile, log, why) ? T_PASS : T_FAIL;
        remove_tree(tmp);
        return r;
    }
    CompileOpts co = {1, 0, cpath};
    int threads = 0, has_main = 0;
    char *c = compile_to_c(path, &co, &threads, &has_main);
    Outcome result = T_PASS;
    if (!c) {
        *why = xstrdup("compile error");
        result = T_FAIL;
    } else if (!has_main) {
        result = T_SKIP;
    } else {
        file_write(cpath, c);
        CcOpts cc = {0};
        char *cflags = NULL, *ldflags = NULL;
        project_flags_for(dir, &cflags, &ldflags, &cc.includes, &cc.sources);
        cc.cflags = cflags;
        cc.ldflags = ldflags;
        cc.output = bin;
        cc.threads = threads;
        cc.sanitize = sanitize;
        cc.release = release;
        vec_push(cc.sources, cpath);
        if (cc_run(&cc) != 0) {
            *why = xstrdup("C compile error");
            result = T_FAIL;
        } else {
            int rc = run_capture(bin, out, sanitize);
            char *exp_path = expected_path(path);
            char *got = file_read(out);
            size_t pn = strlen(path);
            char *code_path = xfmt("%.*s.code", (int)(pn - 4), path);
            int want_rc = 0;
            if (file_exists(code_path)) {
                char *code = file_read(code_path);
                want_rc = atoi(code);
            }
            if (rc != want_rc) {
                *why = xfmt("exit code %d, expected %d", rc, want_rc);
                result = T_FAIL;
            } else if (file_exists(exp_path)) {
                char *want = file_read(exp_path);
                if (!str_eq(want, got ? got : "")) {
                    *why = xfmt("output differs from %s", exp_path);
                    result = T_FAIL;
                    fprintf(stderr, "--- expected\n%s--- got\n%s", want, got ? got : "");
                }
            }
            if (result == T_FAIL && got && *got && !file_exists(exp_path)) fputs(got, stderr);
        }
    }
    remove_tree(tmp);
    return result;
}

int run_tests(StrList *paths, int sanitize, int release) {
    StrList files = {0};
    for (int i = 0; i < paths->len; i++) {
        const char *p = paths->items[i];
        if (dir_exists(p)) walk(p, 1, &files);
        else vec_push(files, xstrdup(p));
    }
    if (!paths->len) walk(".", 0, &files);
    if (!files.len) {
        printf("no tests found (put them in tests/ or name them *.test.mxy)\n");
        return 0;
    }
    int passed = 0, failed = 0, skipped = 0;
    double start = now_ms();
    StrList failures = {0};
    for (int i = 0; i < files.len; i++) {
        double t0 = now_ms();
        char *why = NULL;
        Outcome r = run_one(files.items[i], sanitize, release, &why);
        if (r == T_SKIP) {
            skipped++;
            continue;
        }
        printf("  test %s ... ", files.items[i]);
        if (r == T_PASS) {
            passed++;
            printf("ok (%.0fms)\n", now_ms() - t0);
        } else {
            failed++;
            printf("FAILED (%s)\n", why ? why : "?");
            vec_push(failures, files.items[i]);
        }
        fflush(stdout);
    }
    printf("\n  %d passed, %d failed", passed, failed);
    if (skipped) printf(", %d skipped (no main)", skipped);
    printf(" in %.1fs\n", (now_ms() - start) / 1000.0);
    for (int i = 0; i < failures.len; i++) printf("    failed: %s\n", failures.items[i]);
    return failed ? 1 : 0;
}

int cmd_test_files(int argc, char **argv) {
    StrList paths = {0};
    int sanitize = 0, release = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--sanitize") == 0) sanitize = 1;
        else if (strcmp(argv[i], "--release") == 0 || strcmp(argv[i], "-r") == 0) release = 1;
        else if (str_starts(argv[i], "--enable-")) continue;
        else vec_push(paths, argv[i]);
    }
    return run_tests(&paths, sanitize, release);
}
