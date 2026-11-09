#ifndef MOXY_GEN_H
#define MOXY_GEN_H

typedef struct {
    int line_directives; /* map C lines back to .mxy sources */
    const char *c_name;  /* name of the generated C file, for #line resync */
} GenOptions;

/* generate C for the program last checked by sema_check */
char *codegen(const GenOptions *opts);

#endif
