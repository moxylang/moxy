#include <string.h>

#include "diag/diag.h"
#include "sema.h"

Scope *global_scope;
Scope *cur_scope;
FnCtx *cur_fn;
static StrMap tags;

Scope *scope_push(int kind) {
    Scope *s = xcalloc(1, sizeof(Scope));
    s->parent = cur_scope;
    s->kind = kind;
    s->ns = cur_scope ? cur_scope->ns : xstrdup("");
    cur_scope = s;
    return s;
}

void scope_pop(void) { cur_scope = cur_scope->parent; }

Sym *sym_new(SymKind kind, const char *name, Loc loc) {
    Sym *s = xcalloc(1, sizeof(Sym));
    s->kind = kind;
    s->name = xstrdup(name);
    s->cname = xstrdup(name);
    s->loc = loc;
    return s;
}

Sym *scope_get(Scope *s, const char *name) { return strmap_get(&s->names, name); }

Sym *scope_declare(Scope *s, Sym *sym) {
    strmap_put(&s->names, sym->name, sym);
    return sym;
}

char *cname_for(const char *ns, const char *name) {
    if (!ns || !*ns) return xstrdup(name);
    Buf b = {0};
    for (const char *p = ns; *p; p++) {
        if (p[0] == ':' && p[1] == ':') {
            buf_put(&b, "__");
            p++;
        } else {
            buf_putc(&b, *p);
        }
    }
    buf_fmt(&b, "__%s", name);
    return buf_take(&b);
}

/* the scope for a namespace path, created on demand under the global scope */
Scope *ns_scope(const char *path) {
    if (!path || !*path) return global_scope;
    Scope *parent = global_scope;
    char *copy = xstrdup(path);
    char *seg = copy;
    Buf full = {0};
    for (;;) {
        char *sep = strstr(seg, "::");
        if (sep) *sep = '\0';
        if (full.len) buf_put(&full, "::");
        buf_put(&full, seg);
        Sym *ns = scope_get(parent, seg);
        if (!ns || ns->kind != S_NS) {
            ns = sym_new(S_NS, seg, (Loc){0});
            ns->members = xcalloc(1, sizeof(Scope));
            ns->members->parent = parent;
            ns->members->kind = SC_NS;
            ns->members->ns = xstrdup(buf_str(&full));
            scope_declare(parent, ns);
        }
        parent = ns->members;
        if (!sep) break;
        seg = sep + 2;
    }
    buf_free(&full);
    free(copy);
    return parent;
}

/* captures: a local from an enclosing function referenced inside a lambda */
static void note_capture(Sym *s) {
    if (!s->owner || s->owner == cur_fn || s->kind != S_VAR) return;
    for (FnCtx *f = cur_fn; f && f != s->owner; f = f->outer) {
        if (!f->is_lambda) break;
        int found = 0;
        for (int i = 0; i < f->node->captures.len; i++)
            if (f->node->captures.items[i] == s) found = 1;
        if (!found) vec_push(f->node->captures, s);
    }
    s->captured = 1;
}

static Sym *lookup_in(Scope *s, const char *name) {
    for (; s; s = s->parent) {
        Sym *found = scope_get(s, name);
        if (found) return found;
        for (int i = 0; i < s->imports.len; i++) {
            found = scope_get(s->imports.items[i], name);
            if (found) return found;
        }
    }
    return NULL;
}

static Sym *lookup_path(const char *path) {
    if (!strstr(path, "::")) {
        Sym *s = lookup_in(cur_scope, path);
        if (s && s->owner && s->kind == S_VAR) note_capture(s);
        return s;
    }
    char *copy = xstrdup(path);
    char *seg = copy;
    char *sep = strstr(seg, "::");
    *sep = '\0';
    Sym *s = lookup_in(cur_scope, seg);
    seg = sep + 2;
    if (s && s->kind != S_NS) s = NULL;
    while (s && s->kind == S_NS) {
        sep = strstr(seg, "::");
        if (sep) *sep = '\0';
        s = scope_get(s->members, seg);
        if (!sep) break;
        seg = sep + 2;
        if (s && s->kind != S_NS) {
            /* Enum::Variant beyond a namespace: the caller handles variants */
            s = NULL;
            break;
        }
    }
    free(copy);
    return s;
}

Sym *lookup_quiet(const char *name) { return lookup_path(name); }

Sym *lookup(const char *name, Loc loc) {
    (void)loc;
    Sym *s = lookup_path(name);
    if (s) s->used = 1;
    return s;
}

Sym *lookup_tag(const char *name) { return strmap_get(&tags, name); }

void declare_tag(const char *name, Sym *sym) { strmap_put(&tags, name, sym); }

void tags_reset(void) { strmap_free(&tags); }

static void collect_names(Scope *s, const char *name, const char **best, int *best_d) {
    for (; s; s = s->parent) {
        for (int i = 0; i < s->names.cap; i++) {
            const char *k = s->names.keys[i];
            if (!k) continue;
            Sym *sym = s->names.vals[i];
            if (sym->kind == S_NS) continue;
            int d = str_distance(name, k);
            if (d > 0 && d < *best_d) {
                *best_d = d;
                *best = k;
            }
        }
    }
}

/* "did you mean" for identifiers Moxy cannot see */
void suggest_name(const char *name, Loc loc) {
    if (strlen(name) < 3) return;
    const char *best = NULL;
    int best_d = strlen(name) > 5 ? 3 : 2;
    collect_names(cur_scope, name, &best, &best_d);
    if (best) {
        diag_warn(loc, "'%s' is not declared in Moxy code; assuming it comes from C", name);
        diag_hint("did you mean '%s'?", best);
    }
}
