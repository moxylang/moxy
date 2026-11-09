#include "token.h"

const char *tok_spell(TokKind kind) {
    switch (kind) {
    case P_LBRACE: return "{";
    case P_RBRACE: return "}";
    case P_LPAREN: return "(";
    case P_RPAREN: return ")";
    case P_LBRACKET: return "[";
    case P_RBRACKET: return "]";
    case P_SEMI: return ";";
    case P_COMMA: return ",";
    case P_DOT: return ".";
    case P_ELLIPSIS: return "...";
    case P_DOTDOT: return "..";
    case P_DOTDOTEQ: return "..=";
    case P_ARROW: return "->";
    case P_FATARROW: return "=>";
    case P_COLONCOLON: return "::";
    case P_COLON: return ":";
    case P_QUESTION: return "?";
    case P_AT: return "@";
    case P_PLUS: return "+";
    case P_MINUS: return "-";
    case P_STAR: return "*";
    case P_SLASH: return "/";
    case P_PERCENT: return "%";
    case P_AMP: return "&";
    case P_PIPE: return "|";
    case P_CARET: return "^";
    case P_TILDE: return "~";
    case P_BANG: return "!";
    case P_ASSIGN: return "=";
    case P_LT: return "<";
    case P_GT: return ">";
    case P_LE: return "<=";
    case P_GE: return ">=";
    case P_EQ: return "==";
    case P_NE: return "!=";
    case P_ANDAND: return "&&";
    case P_OROR: return "||";
    case P_SHL: return "<<";
    case P_SHR: return ">>";
    case P_INC: return "++";
    case P_DEC: return "--";
    case P_PIPEGT: return "|>";
    case P_PLUSEQ: return "+=";
    case P_MINUSEQ: return "-=";
    case P_STAREQ: return "*=";
    case P_SLASHEQ: return "/=";
    case P_PERCENTEQ: return "%=";
    case P_AMPEQ: return "&=";
    case P_PIPEEQ: return "|=";
    case P_CARETEQ: return "^=";
    case P_SHLEQ: return "<<=";
    case P_SHREQ: return ">>=";
    default: return "?";
    }
}

const char *tok_name(TokKind kind) {
    switch (kind) {
    case T_EOF: return "end of file";
    case T_IDENT: return "identifier";
    case T_INT: return "integer literal";
    case T_FLOAT: return "float literal";
    case T_STR: return "string literal";
    case T_CHAR: return "character literal";
    case T_DIRECTIVE: return "preprocessor directive";
    case T_UNKNOWN: return "unknown token";
    default: break;
    }
    if (kind >= K_AUTO && kind <= K_NULL) return "keyword";
    static char buf[16][8];
    static int slot;
    char *b = buf[slot++ % 16];
    const char *s = tok_spell(kind);
    b[0] = '\'';
    int i = 1;
    for (; *s && i < 5; s++) b[i++] = *s;
    b[i++] = '\'';
    b[i] = '\0';
    return b;
}
