#include <string.h>

#include "internal.h"

char *cond_text(Node *cond, char *s);

static int dirty(Ctx *c) { return c->pre.len || c->temps.len; }

static void put_pre(Ctx *c, Buf *b, Loc loc) {
    if (!c->pre.len) return;
    const char *p = buf_str(&c->pre);
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        mark_loc(b, loc);
        buf_putn(b, p, n);
        buf_putc(b, '\n');
        p += n + (e ? 1 : 0);
    }
}

static void body_in_loop(Node *body, Buf *b, int indent) {
    gscope_push(GS_BLOCK);
    if (body->kind == N_BLOCK) gen_block_body(&body->list, b, indent);
    else gen_stmt(body, b, indent);
    gscope_pop(b, indent);
}

/* evaluates the condition inside the loop and breaks when it fails */
static void inline_cond(Node *cond, Buf *b, int indent) {
    Ctx c;
    ctx_init(&c, indent);
    char *v = gen_value(cond, &c);
    char *name = fresh("c");
    put_pre(&c, b, cond->loc);
    line_at(b, indent, cond->loc, "bool %s = (%s) ? 1 : 0;", name, v);
    ctx_release(&c, b);
    raw_line(b, indent, "if (!%s) break;", name);
    ctx_free(&c);
}

static void gen_while(Node *n, Buf *b, int indent) {
    Ctx c;
    ctx_init(&c, indent + 1);
    char *v = gen_value(n->a, &c);
    int complex = dirty(&c);
    ctx_free(&c);
    gscope_push(GS_LOOP);
    if (!complex) {
        line_at(b, indent, n->loc, "while (%s) {", cond_text(n->a, v));
    } else {
        line_at(b, indent, n->loc, "while (1) {");
        inline_cond(n->a, b, indent + 1);
    }
    body_in_loop(n->b, b, indent + 1);
    raw_line(b, indent, "}");
    gscope_pop(b, indent);
}

static void gen_do(Node *n, Buf *b, int indent) {
    Ctx c;
    ctx_init(&c, indent + 1);
    char *v = gen_value(n->b, &c);
    int complex = dirty(&c);
    ctx_free(&c);
    gscope_push(GS_LOOP);
    char *label = complex ? fresh("cont") : NULL;
    vec_last(gscopes).continue_label = label;
    line_at(b, indent, n->loc, "do {");
    body_in_loop(n->a, b, indent + 1);
    if (complex) {
        raw_line(b, indent, "%s:;", label);
        inline_cond(n->b, b, indent + 1);
        raw_line(b, indent, "} while (1);");
    } else {
        raw_line(b, indent, "} while (%s);", cond_text(n->b, v));
    }
    gscope_pop(b, indent);
}

static void gen_for(Node *n, Buf *b, int indent) {
    raw_line(b, indent, "{");
    gscope_push(GS_BLOCK);
    int in = indent + 1;
    if (n->a) gen_stmt(n->a, b, in);
    char *cond = NULL;
    int cond_complex = 0;
    if (n->b) {
        Ctx c;
        ctx_init(&c, in + 1);
        cond = gen_value(n->b, &c);
        cond_complex = dirty(&c);
        ctx_free(&c);
    }
    char *step = NULL;
    int step_complex = 0;
    if (n->c) {
        Ctx c;
        ctx_init(&c, in + 1);
        CExpr e = gen_expr(n->c, &c);
        step = e.s;
        step_complex = dirty(&c) || (e.owned && managed(n->c->type));
        ctx_free(&c);
    }
    gscope_push(GS_LOOP);
    char *label = step_complex ? fresh("cont") : NULL;
    vec_last(gscopes).continue_label = label;
    line_at(b, in, n->loc, "for (; %s; %s) {", cond && !cond_complex ? cond_text(n->b, cond) : "",
            step && !step_complex ? step : "");
    if (cond_complex) inline_cond(n->b, b, in + 1);
    body_in_loop(n->d, b, in + 1);
    if (step_complex) {
        raw_line(b, in, "%s:;", label);
        Node es = {0};
        es.kind = N_EXPR;
        es.loc = n->c->loc;
        es.a = n->c;
        gen_stmt(&es, b, in + 1);
    }
    raw_line(b, in, "}");
    gscope_pop(b, in);
    gscope_pop(b, in);
    raw_line(b, indent, "}");
}

static Sym *second_var(Node *n) { return n->captures.len ? n->captures.items[0] : NULL; }

/* binds a loop variable to a borrowed element; mutated bindings own a copy */
static void bind_elem(Sym *s, const char *value, Buf *b, int indent, Loc loc) {
    if (!s) return;
    if (managed(s->type) && s->mutated) {
        line_at(b, indent, loc, "%s = %s;", cdecl(s->type, s->cname), retain_of(s->type, value));
        add_cleanup_var(s->cname, s->type, s);
    } else {
        line_at(b, indent, loc, "%s = %s;", cdecl(s->type, s->cname), value);
    }
}

static void gen_forin(Node *n, Buf *b, int indent) {
    Node *it = n->a;
    Sym *v1 = n->sym, *v2 = second_var(n);
    raw_line(b, indent, "{");
    gscope_push(GS_BLOCK);
    int in = indent + 1;
    if (it->kind == N_RANGE) {
        Ctx c;
        ctx_init(&c, in);
        char *start = gen_value(it->a, &c);
        char *end = gen_value(it->b, &c);
        char *e = fresh("end");
        char *T = ctype(it->type);
        put_pre(&c, b, n->loc);
        line_at(b, in, n->loc, "%s %s = %s;", T, e, end);
        ctx_release(&c, b);
        ctx_free(&c);
        gscope_push(GS_LOOP);
        line_at(b, in, n->loc, "for (%s %s = %s; %s %s %s; %s++) {", T, v1->cname, start, v1->cname,
                (it->flags & F_INCLUSIVE) ? "<=" : "<", e, v1->cname);
        body_in_loop(n->d, b, in + 1);
        raw_line(b, in, "}");
        gscope_pop(b, in);
        gscope_pop(b, in);
        raw_line(b, indent, "}");
        return;
    }
    Type *ct = type_unqual(it->type);
    Ctx c;
    ctx_init(&c, in);
    char *coll;
    char *snap = fresh("it");
    if (managed(ct)) {
        coll = gen_consume(it, &c);
        put_pre(&c, b, n->loc);
        line_at(b, in, n->loc, "%s = %s;", cdecl(ct, snap), coll);
        add_cleanup_var(snap, ct, NULL);
    } else {
        coll = gen_value(it, &c);
        put_pre(&c, b, n->loc);
        snap = coll;
    }
    ctx_release(&c, b);
    ctx_free(&c);
    char *i = fresh("i");
    gscope_push(GS_LOOP);
    switch (ct->kind) {
    case TY_LIST:
        line_at(b, in, n->loc, "for (int64_t %s = 0; %s < mxy_list_len(%s); %s++) {", i, i, snap, i);
        gscope_push(GS_BLOCK);
        if (v2) {
            line_at(b, in + 1, n->loc, "long %s = (long)%s;", v1->cname, i);
            bind_elem(v2, xfmt("*(%s *)MXY_AT(%s, %s)", ctype(ct->base), snap, i), b, in + 1, n->loc);
        } else {
            bind_elem(v1, xfmt("*(%s *)MXY_AT(%s, %s)", ctype(ct->base), snap, i), b, in + 1, n->loc);
        }
        break;
    case TY_STRING:
        line_at(b, in, n->loc, "for (int64_t %s = 0; %s < mxy_str_len(%s); %s++) {", i, i, snap, i);
        gscope_push(GS_BLOCK);
        if (v2) {
            line_at(b, in + 1, n->loc, "long %s = (long)%s;", v1->cname, i);
            line_at(b, in + 1, n->loc, "char %s = %s[%s];", v2->cname, snap, i);
        } else {
            line_at(b, in + 1, n->loc, "char %s = %s[%s];", v1->cname, snap, i);
        }
        break;
    case TY_MAP:
        line_at(b, in, n->loc, "for (int64_t %s = 0; %s < mxy_map_slots(%s); %s++) {", i, i, snap, i);
        raw_line(b, in + 1, "if (!mxy_map_live(%s, %s)) continue;", snap, i);
        gscope_push(GS_BLOCK);
        bind_elem(v1, xfmt("*(%s *)MXY_KEY(%s, %s)", ctype(ct->key), snap, i), b, in + 1, n->loc);
        if (v2) bind_elem(v2, xfmt("*(%s *)MXY_VAL(%s, %s)", ctype(ct->base), snap, i), b, in + 1, n->loc);
        break;
    default: {
        char *len = ct->len >= 0 ? xfmt("%ld", ct->len) : xfmt("(long)(sizeof(%s) / sizeof(%s[0]))", snap, snap);
        line_at(b, in, n->loc, "for (long %s = 0; %s < %s; %s++) {", i, i, len, i);
        gscope_push(GS_BLOCK);
        if (v2) {
            line_at(b, in + 1, n->loc, "long %s = %s;", v1->cname, i);
            bind_elem(v2, xfmt("%s[%s]", snap, i), b, in + 1, n->loc);
        } else {
            bind_elem(v1, xfmt("%s[%s]", snap, i), b, in + 1, n->loc);
        }
        break;
    }
    }
    Node *body = n->d;
    if (body->kind == N_BLOCK) gen_block_body(&body->list, b, in + 1);
    else gen_stmt(body, b, in + 1);
    gscope_pop(b, in + 1);
    raw_line(b, in, "}");
    gscope_pop(b, in);
    gscope_pop(b, in);
    raw_line(b, indent, "}");
}

void gen_loop(Node *n, Buf *b, int indent) {
    switch (n->kind) {
    case N_WHILE: gen_while(n, b, indent); break;
    case N_DO: gen_do(n, b, indent); break;
    case N_FOR: gen_for(n, b, indent); break;
    default: gen_forin(n, b, indent); break;
    }
}
