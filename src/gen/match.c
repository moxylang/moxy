#include <string.h>

#include "internal.h"

static void bind(Node *id, const char *value, Buf *b, int indent, Loc loc) {
    Sym *s = id->sym;
    if (!s) return;
    if (managed(s->type) && s->mutated) {
        line_at(b, indent, loc, "%s = %s;", cdecl(s->type, s->cname), retain_of(s->type, value));
        add_cleanup_var(s->cname, s->type, s);
    } else {
        line_at(b, indent, loc, "%s = %s;", cdecl(s->type, s->cname), value);
        line_at(b, indent, loc, "(void)%s;", s->cname);
    }
}

static char *arm_cond(Node *arm, Type *st, const char *m, Ctx *c) {
    Type *u = type_unqual(st);
    if (u->kind == TY_ENUM) {
        char *variant = xfmt("%s_%s", u->cname, arm->d ? arm->d->name : "?");
        return u->simple ? xfmt("%s == %s", m, variant) : xfmt("%s.tag == %s", m, variant);
    }
    if (u->kind == TY_RESULT) return xfmt(arm->op == 0 ? "%s.ok" : "!%s.ok", m);
    char *v = gen_value(arm->b, c);
    if (u->kind == TY_STRING) return xfmt("mxy_str_eq(%s, %s)", m, v);
    return xfmt("%s == (%s)", m, v);
}

static void arm_bindings(Node *arm, Type *st, const char *m, Buf *b, int indent) {
    Type *u = type_unqual(st);
    if (u->kind == TY_ENUM && arm->d) {
        for (int i = 0; i < arm->list.len && i < arm->d->params.len; i++) {
            char *v = xfmt("%s.%s.%s", m, arm->d->name, arm->d->params.items[i].name);
            bind(arm->list.items[i], v, b, indent, arm->loc);
        }
    } else if (u->kind == TY_RESULT && arm->list.len) {
        bind(arm->list.items[0], xfmt(arm->op == 0 ? "%s.val" : "%s.err", m), b, indent, arm->loc);
    }
}

static int exhaustive_kind(Type *t) {
    Type *u = type_unqual(t);
    return u->kind == TY_ENUM || u->kind == TY_RESULT || u->kind == TY_BOOL;
}

void gen_match(Node *n, Buf *b, int indent) {
    Type *st = n->a->type;
    raw_line(b, indent, "{");
    gscope_push(GS_BLOCK);
    int in = indent + 1;
    Ctx c;
    ctx_init(&c, in);
    char *v = managed(st) ? gen_consume(n->a, &c) : gen_value(n->a, &c);
    char *m = fresh("m");
    if (c.pre.len) {
        const char *p = buf_str(&c.pre);
        mark_loc(b, n->loc);
        buf_put(b, p);
    }
    line_at(b, in, n->loc, "%s = %s;", cdecl(st, m), v);
    ctx_release(&c, b);
    ctx_free(&c);
    if (managed(st)) add_cleanup_var(m, st, NULL);
    int opened = 0;
    for (int i = 0; i < n->list.len; i++) {
        Node *arm = n->list.items[i];
        int wildcard = arm->name && strcmp(arm->name, "_") == 0;
        /* sema proved the match exhaustive, so the final arm needs no test */
        int last = i == n->list.len - 1;
        if (last && !wildcard && exhaustive_kind(st)) wildcard = 2;
        if (wildcard) {
            if (opened) raw_line(b, in, "} else {");
            else raw_line(b, in, "{");
        } else {
            Ctx pc;
            ctx_init(&pc, in);
            char *cond = arm_cond(arm, st, m, &pc);
            if (pc.pre.len) buf_put(b, buf_str(&pc.pre));
            ctx_free(&pc);
            line_at(b, in, arm->loc, opened ? "} else if (%s) {" : "if (%s) {", cond);
        }
        opened = 1;
        gscope_push(GS_BLOCK);
        arm_bindings(arm, st, m, b, in + 1);
        Node *body = arm->a;
        if (body->kind == N_BLOCK) gen_block_body(&body->list, b, in + 1);
        else gen_stmt(body, b, in + 1);
        gscope_pop(b, in + 1);
        if (wildcard == 1) break;
    }
    if (opened) raw_line(b, in, "}");
    gscope_pop(b, in);
    raw_line(b, indent, "}");
}
