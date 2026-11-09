#ifndef MOXY_CONF_H
#define MOXY_CONF_H

typedef struct {
    int indent;
    int brace_knr;
    int space_around_ops;
    int space_after_comma;
    int space_after_keyword;
    int trailing_newline;
    int max_line_length;
    int lint_unused_vars;
    int lint_empty_blocks;
    int lint_shadow_vars;
} MoxyConfig;

MoxyConfig mxyconf_defaults(void);
MoxyConfig mxyconf_load(const char *path);
char *mxyconf_find(const char *start_dir, const char *file_dir);

#endif
