#include <string.h>

#include "lex/lexer.h"
#include "state.h"

static Node *parse_postfix(void);

static Node *mk(NodeKind kind, Loc loc) { return node_new(kind, loc); }

static int binary_prec(TokKind k) {
    switch (k) {
    case P_PIPEGT: return 1;
    case P_OROR: return 2;
    case P_ANDAND: return 3;
    case P_PIPE: return 4;
    case P_CARET: return 5;
    case P_AMP: return 6;
    case P_EQ: case P_NE: return 7;
    case P_LT: case P_GT: case P_LE: case P_GE: return 8;
    case P_SHL: case P_SHR: return 9;
    case P_PLUS: case P_MINUS: return 10;
    case P_STAR: case P_SLASH: case P_PERCENT: return 11;
    default: return 0;
    }
}

static int is_assign_op(TokKind k) {
    switch (k) {
    case P_ASSIGN: case P_PLUSEQ: case P_MINUSEQ: case P_STAREQ: case P_SLASHEQ:
    case P_PERCENTEQ: case P_AMPEQ: case P_PIPEEQ: case P_CARETEQ: case P_SHLEQ: case P_SHREQ:
        return 1;
    default:
        return 0;
    }
}

static Node *pipe_into(Node *left, Node *right, Loc loc) {
    if (right->kind == N_CALL) {
        vec_push(right->list, NULL);
        for (int i = right->list.len - 1; i > 0; i--) right->list.items[i] = right->list.items[i - 1];
        right->list.items[0] = left;
        return right;
    }
    if (right->kind == N_IDENT || right->kind == N_MEMBER) {
        Node *call = mk(N_CALL, right->loc);
        call->a = right;
        vec_push(call->list, left);
        return call;
    }
    fail(loc, "expected a function call after '|>'");
}

static Node *parse_binary(int min) {
    Node *left = parse_unary();
    for (;;) {
        TokKind k = peek()->kind;
        int prec = binary_prec(k);
        if (!prec || prec < min) break;
        Token *op = next();
        if (k == P_PIPEGT) {
            Node *right = parse_postfix();
            left = pipe_into(left, right, op->loc);
            continue;
        }
        Node *right = parse_binary(prec + 1);
        Node *n = mk(N_BINARY, op->loc);
        n->op = k;
        n->a = left;
        n->b = right;
        left = n;
    }
    return left;
}

Node *parse_ternary(void) {
    Node *cond = parse_binary(1);
    if (!at(P_QUESTION)) return cond;
    Token *q = next();
    Node *n = mk(N_TERNARY, q->loc);
    n->a = cond;
    n->b = parse_expr();
    expect(P_COLON, "':' in conditional expression");
    n->c = parse_ternary();
    return n;
}

Node *parse_assign(void) {
    Node *left = parse_ternary();
    if (!is_assign_op(peek()->kind)) return left;
    Token *op = next();
    Node *n = mk(N_ASSIGN, op->loc);
    n->op = op->kind;
    n->a = left;
    n->b = at(P_LBRACE) ? parse_initializer() : parse_assign();
    return n;
}

Node *parse_expr(void) {
    Node *left = parse_assign();
    while (at(P_COMMA)) {
        Token *c = next();
        Node *n = mk(N_COMMA, c->loc);
        n->a = left;
        n->b = parse_assign();
        left = n;
    }
    return left;
}

/* { a, .x = b, [2] = c, "k": v } */
Node *parse_initializer(void) {
    if (!at(P_LBRACE)) return parse_assign();
    Token *open = next();
    Node *list = mk(N_INITLIST, open->loc);
    while (!at(P_RBRACE)) {
        Loc loc = peek()->loc;
        if (at(P_DOT) && peek_at(1)->kind == T_IDENT) {
            next();
            Node *d = mk(N_DESIG, loc);
            d->name = xstrdup(next()->text);
            while (at(P_DOT) && peek_at(1)->kind == T_IDENT) {
                next();
                char *joined = xfmt("%s.%s", d->name, next()->text);
                free(d->name);
                d->name = joined;
            }
            expect(P_ASSIGN, "'=' after designator");
            d->b = parse_initializer();
            vec_push(list->list, d);
        } else if (at(P_LBRACKET)) {
            next();
            Node *d = mk(N_DESIG, loc);
            d->a = parse_ternary();
            expect(P_RBRACKET, "']'");
            expect(P_ASSIGN, "'=' after designator");
            d->b = parse_initializer();
            vec_push(list->list, d);
        } else {
            Node *v = parse_initializer();
            if (accept(P_COLON)) {
                Node *kv = mk(N_KV, loc);
                kv->a = v;
                kv->b = parse_initializer();
                v = kv;
            }
            vec_push(list->list, v);
        }
        if (!accept(P_COMMA)) break;
    }
    expect(P_RBRACE, "'}'");
    return list;
}

static int lambda_ahead(void) {
    if (!at(P_LPAREN)) return 0;
    int close = match_paren(P.pos);
    return close > 0 && P.toks[close + 1].kind == P_FATARROW;
}

static Node *parse_lambda(void) {
    Token *open = next();
    Node *n = mk(N_LAMBDA, open->loc);
    TypeRefList types = {0};
    int variadic = 0;
    parse_params(&n->params, &types, &variadic);
    free(types.items);
    for (int i = 0; i < n->params.len; i++)
        if (!n->params.items[i].name) fail(n->params.items[i].loc, "lambda parameters need names");
    expect(P_FATARROW, "'=>'");
    if (at(P_LBRACE)) {
        n->a = parse_block();
    } else {
        n->a = parse_assign();
        n->flags |= F_EXPRBODY;
    }
    return n;
}

/* split "a \(x) b" into string pieces and expressions */
static Node *parse_interp(Node *str) {
    const char *s = str->text;
    Node *n = mk(N_INTERP, str->loc);
    Buf piece = {0};
    for (int i = 0; s[i];) {
        if (s[i] == '\\' && s[i + 1] == '(') {
            int start = i + 2, depth = 1, j = start;
            while (s[j] && depth > 0) {
                if (s[j] == '"' || s[j] == '\'') {
                    char q = s[j++];
                    while (s[j] && s[j] != q) {
                        if (s[j] == '\\' && s[j + 1]) j++;
                        j++;
                    }
                    if (s[j]) j++;
                    continue;
                }
                if (s[j] == '(') depth++;
                if (s[j] == ')') depth--;
                if (depth > 0) j++;
            }
            if (depth != 0) fail(str->loc, "unterminated '\\(' in string interpolation");
            if (piece.len) {
                Node *p = mk(N_STR, str->loc);
                p->text = buf_take(&piece);
                vec_push(n->list, p);
            }
            char *src = xstrndup(s + start, (size_t)(j - start));
            Loc where = str->loc;
            where.col += 1 + start;
            TokenVec toks = lex_fragment(src, where);
            Parser saved = P;
            P.toks = toks.items;
            P.len = toks.len;
            P.pos = 0;
            Node *e = parse_expr();
            if (!at(T_EOF)) fail(peek()->loc, "unexpected %s in interpolation", peek()->text);
            P = saved;
            vec_push(n->list, e);
            free(src);
            i = j + 1;
            continue;
        }
        if (s[i] == '\\' && s[i + 1]) {
            buf_putn(&piece, s + i, 2);
            i += 2;
            continue;
        }
        buf_putc(&piece, s[i++]);
    }
    if (piece.len || n->list.len == 0) {
        Node *p = mk(N_STR, str->loc);
        p->text = buf_take(&piece);
        vec_push(n->list, p);
    }
    buf_free(&piece);
    return n;
}

static int has_interp(const char *s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\\' && s[i + 1] == '(') return 1;
        if (s[i] == '\\' && s[i + 1]) i++;
    }
    return 0;
}

static Node *parse_string(void) {
    Token *t = next();
    Node *n = mk(N_STR, t->loc);
    if (!at(T_STR)) {
        n->text = xstrdup(t->text);
    } else {
        Buf b = {0};
        buf_put(&b, t->text);
        while (at(T_STR)) {
            buf_put(&b, "\" \"");
            buf_put(&b, next()->text);
        }
        n->text = buf_take(&b);
    }
    if (has_interp(n->text)) return parse_interp(n);
    return n;
}

static char *parse_path(Token *first) {
    Buf b = {0};
    buf_put(&b, first->text);
    while (at(P_COLONCOLON) && peek_at(1)->kind == T_IDENT) {
        next();
        buf_put(&b, "::");
        buf_put(&b, next()->text);
    }
    return buf_take(&b);
}

static Node *parse_primary(void) {
    Token *t = peek();
    switch (t->kind) {
    case T_INT: {
        next();
        Node *n = mk(N_INT, t->loc);
        n->text = xstrdup(t->text);
        return n;
    }
    case T_FLOAT: {
        next();
        Node *n = mk(N_FLOAT, t->loc);
        n->text = xstrdup(t->text);
        return n;
    }
    case T_CHAR: {
        next();
        Node *n = mk(N_CHAR, t->loc);
        n->text = xstrdup(t->text);
        return n;
    }
    case T_STR:
        return parse_string();
    case K_TRUE: case K_FALSE: {
        next();
        Node *n = mk(N_BOOL, t->loc);
        n->op = t->kind == K_TRUE;
        return n;
    }
    case K_NULL:
        next();
        return mk(N_NULL, t->loc);
    case K_STRING: {
        /* string(x): conversion to string */
        next();
        if (!at(P_LPAREN)) fail(t->loc, "'string' is a type; did you mean string(value)?");
        Node *id = mk(N_IDENT, t->loc);
        id->name = xstrdup("string");
        return id;
    }
    case P_LBRACKET: {
        next();
        Node *n = mk(N_LIST, t->loc);
        while (!at(P_RBRACKET)) {
            vec_push(n->list, parse_initializer());
            if (!accept(P_COMMA)) break;
        }
        expect(P_RBRACKET, "']' to close the list literal");
        return n;
    }
    case P_LBRACE:
        return parse_initializer();
    case P_LPAREN: {
        if (lambda_ahead()) return parse_lambda();
        int unknown_ptr_cast = 0;
        if (!is_type_start_at(1) && peek_at(1)->kind == T_IDENT && peek_at(2)->kind == P_STAR) {
            int j = 2;
            while (peek_at(j)->kind == P_STAR) j++;
            unknown_ptr_cast = peek_at(j)->kind == P_RPAREN;
        } else if (!is_type_start_at(1) && peek_at(1)->kind == T_IDENT && peek_at(2)->kind == P_RPAREN &&
                   str_ends(peek_at(1)->text, "_t")) {
            TokKind k = peek_at(3)->kind;
            unknown_ptr_cast = k == T_IDENT || k == T_INT || k == T_FLOAT || k == T_CHAR || k == P_LPAREN;
        }
        if (is_type_start_at(1) || unknown_ptr_cast) {
            next();
            TypeRef *type;
            if (unknown_ptr_cast) {
                Token *name = next();
                type = tref_new(TR_NAME, name->loc);
                type->name = xstrdup(name->text);
                while (at(P_STAR)) {
                    TypeRef *p = tref_new(TR_PTR, next()->loc);
                    p->inner = type;
                    type = p;
                }
            } else {
                type = parse_type_name();
            }
            expect(P_RPAREN, "')' after type");
            if (at(P_LBRACE)) {
                Node *n = mk(N_COMPOUND, t->loc);
                n->tref = type;
                n->b = parse_initializer();
                return n;
            }
            Node *n = mk(N_CAST, t->loc);
            n->tref = type;
            n->a = parse_unary();
            return n;
        }
        next();
        int saved = P.no_struct_lit;
        P.no_struct_lit = 0;
        Node *inner = parse_expr();
        P.no_struct_lit = saved;
        expect(P_RPAREN, "')'");
        inner->flags |= F_PARENS;
        return inner;
    }
    case T_IDENT: {
        next();
        if ((strcmp(t->text, "Ok") == 0 || strcmp(t->text, "Err") == 0) && at(P_LPAREN)) {
            Node *n = mk(t->text[0] == 'O' ? N_OK : N_ERR, t->loc);
            next();
            n->a = at(P_RPAREN) ? NULL : parse_assign();
            expect(P_RPAREN, "')'");
            return n;
        }
        char *name = parse_path(t);
        Loc loc = t->loc;
        loc.span = (int)strlen(name);
        if (at(P_LBRACE) && !P.no_struct_lit) {
            const char *last = strrchr(name, ':');
            last = last ? last + 1 : name;
            if (types_has(last)) {
                Node *n = mk(N_STRUCTLIT, loc);
                n->tref = tref_new(TR_NAME, loc);
                n->tref->name = name;
                n->b = parse_initializer();
                return n;
            }
        }
        Node *n = mk(N_IDENT, loc);
        n->name = name;
        return n;
    }
    default:
        break;
    }
    if (t->kind == T_EOF) fail(t->loc, "unexpected end of file in expression");
    fail(t->loc, "unexpected '%s' in expression", t->text);
}

/* a type used as a macro argument: va_arg(ap, int), offsetof(T, f) */
static int type_arg_ahead(void) {
    switch (peek()->kind) {
    case K_VOID: case K_CHAR: case K_SHORT: case K_INT: case K_LONG: case K_FLOAT: case K_DOUBLE:
    case K_SIGNED: case K_UNSIGNED: case K_BOOL: case K_STRUCT: case K_UNION: case K_ENUM: case K_CONST:
    case K_VOLATILE:
        return 1;
    case T_IDENT: {
        if (!is_type_name(peek())) return 0;
        TokKind n = peek_at(1)->kind;
        return n == P_COMMA || n == P_RPAREN || n == P_STAR;
    }
    default:
        return 0;
    }
}

static Node *parse_postfix(void) {
    Node *e = parse_primary();
    for (;;) {
        Token *t = peek();
        if (t->kind == P_LPAREN) {
            next();
            Node *call = mk(N_CALL, e->loc);
            call->a = e;
            int saved = P.no_struct_lit;
            P.no_struct_lit = 0;
            while (!at(P_RPAREN)) {
                if (type_arg_ahead()) {
                    Node *ta = mk(N_TYPEARG, peek()->loc);
                    ta->tref = parse_type_name();
                    vec_push(call->list, ta);
                } else {
                    vec_push(call->list, at(P_LBRACE) ? parse_initializer() : parse_assign());
                }
                if (!accept(P_COMMA)) break;
            }
            P.no_struct_lit = saved;
            expect(P_RPAREN, "')' to close the argument list");
            e = call;
            continue;
        }
        if (t->kind == P_LBRACKET) {
            next();
            Node *n = mk(N_INDEX, t->loc);
            n->a = e;
            n->b = parse_expr();
            expect(P_RBRACKET, "']'");
            e = n;
            continue;
        }
        if (t->kind == P_DOT || t->kind == P_ARROW) {
            next();
            Token *name = peek();
            if (name->kind != T_IDENT && !(name->kind >= K_AUTO && name->kind <= K_NULL))
                fail(name->loc, "expected a field or method name after '%s'", t->text);
            next();
            Node *n = mk(N_MEMBER, name->loc);
            n->op = t->kind;
            n->a = e;
            n->name = xstrdup(name->text);
            e = n;
            continue;
        }
        if (t->kind == P_INC || t->kind == P_DEC) {
            next();
            Node *n = mk(N_POSTFIX, t->loc);
            n->op = t->kind;
            n->a = e;
            e = n;
            continue;
        }
        break;
    }
    return e;
}

static int await_ahead(void) {
    if (!at_word("await")) return 0;
    Token *n = peek_at(1);
    if (n->bol && n->kind != T_IDENT) return 0;
    return n->kind == T_IDENT || n->kind == P_LPAREN;
}

Node *parse_unary(void) {
    Token *t = peek();
    switch (t->kind) {
    case P_MINUS: case P_PLUS: case P_BANG: case P_TILDE: case P_STAR: case P_AMP:
    case P_INC: case P_DEC: {
        next();
        Node *n = mk(N_UNARY, t->loc);
        n->op = t->kind;
        n->a = parse_unary();
        return n;
    }
    case K_SIZEOF: {
        next();
        Node *n = mk(N_SIZEOF, t->loc);
        if (at(P_LPAREN) && is_type_start_at(1)) {
            next();
            n->tref = parse_type_name();
            expect(P_RPAREN, "')'");
        } else {
            n->a = parse_unary();
        }
        return n;
    }
    case K_ALIGNOF: {
        next();
        Node *n = mk(N_ALIGNOF, t->loc);
        expect(P_LPAREN, "'('");
        n->tref = parse_type_name();
        expect(P_RPAREN, "')'");
        return n;
    }
    default:
        break;
    }
    if (await_ahead()) {
        next();
        Node *n = mk(N_AWAIT, t->loc);
        n->a = parse_unary();
        return n;
    }
    return parse_postfix();
}
