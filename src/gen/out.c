#include <stdio.h>
#include <string.h>

#include "diag/diag.h"
#include "internal.h"

Sections out;
const GenOptions *gopts;
int tmp_counter;

static StrMap literals;
static int lit_counter;

char *fresh(const char *prefix) { return xfmt("__%s%d", prefix, ++tmp_counter); }

static void indent_to(Buf *b, int indent) {
    for (int i = 0; i < indent; i++) buf_put(b, "    ");
}

void mark_loc(Buf *b, Loc loc) {
    if (!gopts->line_directives || loc.line <= 0) return;
    const SrcFile *f = diag_file(loc.file);
    if (!f) return;
    buf_fmt(b, "#line %d \"", loc.line);
    for (const char *p = f->path; *p; p++) {
        if (*p == '"' || *p == '\\') buf_putc(b, '\\');
        buf_putc(b, *p);
    }
    buf_put(b, "\"\n");
}

void line_at(Buf *b, int indent, Loc loc, const char *fmt, ...) {
    mark_loc(b, loc);
    indent_to(b, indent);
    va_list ap;
    va_start(ap, fmt);
    buf_vfmt(b, fmt, ap);
    va_end(ap);
    buf_putc(b, '\n');
}

void raw_line(Buf *b, int indent, const char *fmt, ...) {
    indent_to(b, indent);
    va_list ap;
    va_start(ap, fmt);
    buf_vfmt(b, fmt, ap);
    va_end(ap);
    buf_putc(b, '\n');
}

char *c_string_lit(const char *raw) { return xfmt("\"%s\"", raw); }

char *lit_ref(const char *raw) {
    char *name = strmap_get(&literals, raw);
    if (!name) {
        name = xfmt("__mxy_s%d", ++lit_counter);
        strmap_put(&literals, raw, name);
        buf_fmt(&out.lits, "MXY_LIT(%s, \"%s\");\n", name, raw);
    }
    return xfmt("((mxy_str)%s.s)", name);
}

char *loc_lit(Loc loc) {
    const SrcFile *f = diag_file(loc.file);
    Buf b = {0};
    buf_putc(&b, '"');
    const char *path = f ? f->display : "?";
    for (const char *p = path; *p; p++) {
        if (*p == '"' || *p == '\\') buf_putc(&b, '\\');
        buf_putc(&b, *p);
    }
    buf_fmt(&b, ":%d:%d\"", loc.line, loc.col);
    return buf_take(&b);
}

char *sym_ref(Sym *s) {
    char *cap = capture_ref(s);
    if (cap) return cap;
    return xstrdup(s->cname);
}

void out_reset(void) {
    Buf *all[] = {&out.head, &out.directives, &out.fwd, &out.types, &out.hproto, &out.helpers,
                  &out.protos, &out.lits, &out.globals, &out.funcs, &out.init};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) buf_free(all[i]);
    strmap_free(&literals);
    lit_counter = 0;
    tmp_counter = 0;
}
