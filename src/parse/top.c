#include <ctype.h>
#include <string.h>

#include "parse.h"
#include "state.h"

typedef struct {
    char *any;
    char *cur;
} CondFrame;

static Vec(CondFrame) conds;

static char *current_cond(void) {
    if (!conds.len) return NULL;
    Buf b = {0};
    for (int i = 0; i < conds.len; i++) {
        if (i) buf_put(&b, " && ");
        buf_put(&b, conds.items[i].cur);
    }
    return buf_take(&b);
}

static Node *mk(NodeKind kind, Loc loc) { return node_new(kind, loc); }

/* parse one declaration statement: variables, prototypes, typedefs,
   struct/enum definitions, or (top level only) a function definition */
Node *parse_decl_group(int top) {
    Loc loc = peek()->loc;
    Specs sp;
    if (!parse_specifiers(&sp)) fail(loc, "expected a declaration");
    Node *group = mk(N_DECLS, loc);
    if (sp.base->kind == TR_TAG && sp.base->def) {
        vec_push(group->list, sp.base->def);
        /* a definition ending its line needs no ';' */
        if (!at(P_SEMI) && peek()->bol) return group;
    }
    if (accept(P_SEMI)) {
        if (sp.base->kind == TR_TAG && !sp.base->def) {
            Node *fwd = mk(sp.base->tag == K_ENUM ? N_CENUM : N_STRUCT, loc);
            fwd->name = sp.base->name;
            if (sp.base->tag == K_UNION) fwd->flags |= F_UNION;
            vec_push(group->list, fwd);
        }
        return group;
    }
    for (;;) {
        char *name = NULL;
        ParamList fparams = {0};
        Loc nloc = peek()->loc;
        TypeRef *t = parse_declarator(sp.base, &name, &fparams, &nloc);
        if (!name) fail(peek()->loc, "expected a name in declaration, found '%s'", peek()->text);
        if (sp.is_typedef) {
            Node *td = mk(N_TYPEDEF, nloc);
            td->name = name;
            td->tref = t;
            vec_push(group->list, td);
        } else if (t->kind == TR_FUNC) {
            Node *fn = mk(N_FUNC, nloc);
            fn->name = name;
            fn->tref = t->inner;
            fn->params = fparams;
            fn->flags = sp.storage;
            if (t->variadic) fn->op = 1;
            if (at(P_LBRACE)) {
                if (!top) fail(nloc, "functions cannot be defined inside other functions");
                fn->a = parse_block();
                vec_push(group->list, fn);
                return group;
            }
            vec_push(group->list, fn);
        } else {
            Node *v = mk(N_VAR, nloc);
            v->name = name;
            v->tref = t;
            v->flags = sp.storage | (top ? F_GLOBAL : 0);
            if (sp.is_auto) v->op = 1;
            if (accept(P_ASSIGN)) v->a = parse_initializer();
            else if (sp.is_auto) fail(nloc, "'auto' declarations need an initializer");
            vec_push(group->list, v);
        }
        if (!accept(P_COMMA)) break;
    }
    expect(P_SEMI, "';'");
    return group;
}

static char *directive_word(const char *text, const char **rest) {
    const char *p = text + 1;
    while (*p == ' ' || *p == '\t') p++;
    const char *s = p;
    while (isalpha((unsigned char)*p)) p++;
    char *w = xstrndup(s, (size_t)(p - s));
    while (*p == ' ' || *p == '\t') p++;
    *rest = p;
    return w;
}

static void top_directive(Token *t, NodeList *out, const char *ns) {
    const char *rest;
    char *w = directive_word(t->text, &rest);
    if (strcmp(w, "if") == 0 || strcmp(w, "ifdef") == 0 || strcmp(w, "ifndef") == 0) {
        char *c = strcmp(w, "if") == 0       ? xfmt("(%s)", rest)
                  : strcmp(w, "ifdef") == 0  ? xfmt("defined(%s)", rest)
                                             : xfmt("!defined(%s)", rest);
        CondFrame f = {xstrdup(c), c};
        vec_push(conds, f);
    } else if (strcmp(w, "elif") == 0 || strcmp(w, "else") == 0) {
        if (!conds.len) fail(t->loc, "#%s without #if", w);
        CondFrame *f = &vec_last(conds);
        if (strcmp(w, "else") == 0) {
            f->cur = xfmt("!(%s)", f->any);
        } else {
            f->cur = xfmt("(!(%s) && (%s))", f->any, rest);
            f->any = xfmt("(%s) || (%s)", f->any, rest);
        }
    } else if (strcmp(w, "endif") == 0) {
        if (!conds.len) fail(t->loc, "#endif without #if");
        conds.len--;
    } else if (strcmp(w, "pragma") == 0 && str_starts(rest, "once")) {
        /* every .mxy file is included once already */
    } else {
        Node *d = mk(N_DIRECTIVE, t->loc);
        d->text = xstrdup(t->text);
        d->cond = current_cond();
        d->ns = xstrdup(ns);
        vec_push(*out, d);
    }
    free(w);
}

static void at_type(void) {
    next();
    next();
    while (at(T_IDENT)) {
        parse_register_type(next()->text);
        if (!accept(P_COMMA)) break;
    }
    accept(P_SEMI);
}

static void parse_top_list(NodeList *out, const char *ns, int until_brace);

static void add_decl(NodeList *out, Node *n, const char *ns) {
    n->cond = current_cond();
    n->ns = xstrdup(ns);
    vec_push(*out, n);
}

static void parse_top_one(NodeList *out, const char *ns) {
    Token *t = peek();
    if (t->kind == T_DIRECTIVE) {
        next();
        top_directive(t, out, ns);
        return;
    }
    if (accept(P_SEMI)) return;
    if (t->kind == P_AT && word_at(1, "type")) {
        at_type();
        return;
    }
    if (at_word("namespace") && peek_at(1)->kind == T_IDENT &&
        (peek_at(2)->kind == P_LBRACE || peek_at(2)->kind == P_COLONCOLON)) {
        next();
        Buf path = {0};
        if (*ns) {
            buf_put(&path, ns);
            buf_put(&path, "::");
        }
        buf_put(&path, next()->text);
        while (accept(P_COLONCOLON)) {
            buf_put(&path, "::");
            buf_put(&path, expect(T_IDENT, "a namespace name")->text);
        }
        expect(P_LBRACE, "'{'");
        char *inner = buf_take(&path);
        parse_top_list(out, inner, 1);
        expect(P_RBRACE, "'}' to close the namespace");
        free(inner);
        return;
    }
    if (at_word("using") && peek_at(1)->kind == T_IDENT &&
        (peek_at(2)->kind == P_SEMI || peek_at(2)->kind == P_COLONCOLON)) {
        Token *kw = next();
        Buf path = {0};
        buf_put(&path, next()->text);
        while (accept(P_COLONCOLON)) {
            buf_put(&path, "::");
            buf_put(&path, expect(T_IDENT, "a namespace name")->text);
        }
        expect(P_SEMI, "';'");
        Node *u = mk(N_USING, kw->loc);
        u->name = buf_take(&path);
        add_decl(out, u, ns);
        return;
    }
    if (t->kind == K_STATIC_ASSERT) {
        add_decl(out, parse_stmt(), ns);
        return;
    }
    int is_ref = 0;
    if (at_word("ref") && peek_at(1)->kind == K_STRUCT) {
        next();
        is_ref = 1;
    }
    if (moxy_enum_ahead()) {
        add_decl(out, parse_moxy_enum(), ns);
        return;
    }
    Node *group = parse_decl_group(1);
    for (int i = 0; i < group->list.len; i++) {
        Node *d = group->list.items[i];
        if (is_ref && d->kind == N_STRUCT) d->flags |= F_REF;
        add_decl(out, d, ns);
    }
    free(group->list.items);
    free(group);
}

static void parse_top_list(NodeList *out, const char *ns, int until_brace) {
    while (!at(T_EOF) && !(until_brace && at(P_RBRACE))) parse_top_one(out, ns);
}

void parse_register_type(const char *name) { types_add(name); }

Node *parse_program(TokenVec toks) {
    memset(&P, 0, sizeof(P));
    P.toks = toks.items;
    P.len = toks.len;
    conds.len = 0;
    types_reset();
    types_prescan();
    Node *prog = node_new(N_BLOCK, toks.items[0].loc);
    parse_top_list(&prog->list, "", 0);
    if (conds.len) fail(peek()->loc, "unterminated #if block");
    return prog;
}

Node *parse_expression_tokens(TokenVec toks) {
    memset(&P, 0, sizeof(P));
    P.toks = toks.items;
    P.len = toks.len;
    return parse_expr();
}
