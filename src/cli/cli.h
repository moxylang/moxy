#ifndef MOXY_CLI_H
#define MOXY_CLI_H

#include "base/base.h"

#ifdef MOXY_VERSION
#define MOXY_VERSION_STR MOXY_VERSION
#else
#define MOXY_VERSION_STR "dev"
#endif

typedef Vec(char *) StrList;

/* compile.c: .mxy -> C */
typedef struct {
    int line_directives;
    int lint;
    const char *c_name;
} CompileOpts;

char *compile_to_c(const char *path, const CompileOpts *o, int *uses_threads, int *has_main);
int check_file(const char *path, int lint);

/* cc.c: C -> binary */
typedef struct {
    const char *cc;
    const char *cflags;
    const char *ldflags;
    const char *output;
    StrList sources;
    StrList includes;
    int release;
    int threads;
    int object_only;
    int sanitize;
} CcOpts;

int cc_run(const CcOpts *o);
char *shell_quote(const char *s);
char *make_temp_dir(void);
void remove_tree(const char *path);
int run_binary(const char *path, int argc, char **argv);

/* file.c: single-file commands */
int cmd_emit(int argc, char **argv);
int cmd_run_file(int argc, char **argv);
int cmd_build_file(int argc, char **argv);
int cmd_check(int argc, char **argv);

/* test.c */
int run_tests(StrList *paths, int sanitize, int release);
int cmd_test_files(int argc, char **argv);

/* fmtcmd.c / lintcmd.c */
int cmd_fmt(int argc, char **argv);
int cmd_lint(int argc, char **argv);
void collect_mxy(const char *dir, StrList *out);

/* project.c */
int project_main(int argc, char **argv);

#endif
