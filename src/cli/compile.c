#include "cli.h"

#include <setjmp.h>
#include <string.h>

#include "diag/diag.h"
#include "gen/gen.h"
#include "load/load.h"
#include "parse/parse.h"
#include "sema/sema.h"

static int first_file;

/* directories of the .mxy files in the last compile, so their C headers resolve */
void source_dirs(StrList *out) {
    for (int i = first_file; i < diag_file_count(); i++) {
        const SrcFile *f = diag_file(i);
        char *dir = path_dir(f->path);
        int dup = !dir_exists(dir);
        for (int j = 0; j < out->len && !dup; j++)
            if (strcmp(out->items[j], dir) == 0) dup = 1;
        if (dup) free(dir);
        else vec_push(*out, dir);
    }
}

static char *compile_inner(const char *path, const CompileOpts *o, int *uses_threads, int *has_main) {
    first_file = diag_file_count();
    diag_reset();
    load_reset();
    TokenVec toks = load_program(path);
    diag_check();
    Node *root = parse_program(toks);
    sema_check(root, o->lint);
    diag_check();
    GenOptions g = {o->line_directives, o->c_name ? o->c_name : "moxy.c"};
    char *c = codegen(&g);
    if (uses_threads) *uses_threads = prog.threads;
    if (has_main) *has_main = prog.main != NULL;
    return c;
}

static int check_inner(const char *path, int lint) {
    diag_reset();
    load_reset();
    TokenVec toks = load_program(path);
    if (diag_errors()) return diag_errors();
    Node *root = parse_program(toks);
    sema_check(root, lint);
    return diag_errors();
}

/* errors jump back here instead of exiting, so one bad file never stops a test run */
char *compile_to_c(const char *path, const CompileOpts *o, int *uses_threads, int *has_main) {
    jmp_buf jb;
    if (setjmp(jb)) return NULL;
    diag_set_recover(&jb);
    char *c = compile_inner(path, o, uses_threads, has_main);
    diag_set_recover(NULL);
    return c;
}

int check_file(const char *path, int lint) {
    jmp_buf jb;
    if (setjmp(jb)) return diag_errors() ? diag_errors() : 1;
    diag_set_recover(&jb);
    int rc = check_inner(path, lint);
    diag_set_recover(NULL);
    return rc;
}
