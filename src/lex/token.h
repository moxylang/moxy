#ifndef MOXY_TOKEN_H
#define MOXY_TOKEN_H

#include "base/base.h"

typedef enum {
    T_EOF,
    T_IDENT,
    T_INT,
    T_FLOAT,
    T_STR,
    T_CHAR,
    T_DIRECTIVE,

    /* keywords (C plus Moxy's always-reserved words) */
    K_AUTO, K_BREAK, K_CASE, K_CHAR, K_CONST, K_CONTINUE, K_DEFAULT, K_DO,
    K_DOUBLE, K_ELSE, K_ENUM, K_EXTERN, K_FLOAT, K_FOR, K_GOTO, K_IF, K_INLINE,
    K_INT, K_LONG, K_REGISTER, K_RESTRICT, K_RETURN, K_SHORT, K_SIGNED,
    K_SIZEOF, K_STATIC, K_STRUCT, K_SWITCH, K_TYPEDEF, K_UNION, K_UNSIGNED,
    K_VOID, K_VOLATILE, K_WHILE, K_BOOL, K_NORETURN, K_ALIGNOF, K_STATIC_ASSERT,
    K_THREAD_LOCAL, K_STRING, K_TRUE, K_FALSE, K_NULL,

    /* punctuation */
    P_LBRACE, P_RBRACE, P_LPAREN, P_RPAREN, P_LBRACKET, P_RBRACKET,
    P_SEMI, P_COMMA, P_DOT, P_ELLIPSIS, P_DOTDOT, P_DOTDOTEQ, P_ARROW,
    P_FATARROW, P_COLONCOLON, P_COLON, P_QUESTION, P_AT,
    P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT, P_AMP, P_PIPE, P_CARET,
    P_TILDE, P_BANG, P_ASSIGN, P_LT, P_GT, P_LE, P_GE, P_EQ, P_NE,
    P_ANDAND, P_OROR, P_SHL, P_SHR, P_INC, P_DEC, P_PIPEGT,
    P_PLUSEQ, P_MINUSEQ, P_STAREQ, P_SLASHEQ, P_PERCENTEQ, P_AMPEQ,
    P_PIPEEQ, P_CARETEQ, P_SHLEQ, P_SHREQ,
    T_UNKNOWN,
} TokKind;

typedef struct {
    TokKind kind;
    char *text;
    Loc loc;
    int bol;   /* first token on its line */
} Token;

typedef Vec(Token) TokenVec;

const char *tok_name(TokKind kind);
const char *tok_spell(TokKind kind);

#endif
