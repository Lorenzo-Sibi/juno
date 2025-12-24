#include "internal/juno_lex.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ------------------------------
 * Internal helpers
 * ------------------------------ */

static JToken jl_create_token(JLexer *lx, JTokenType type, const char *start, size_t line, size_t col) {
    JToken t;
    memset(&t, 0, sizeof(t));
    t.type   = type;
    t.start  = start;
    t.line   = (uint32_t)line;
    t.column = (uint32_t)col;
    t.length = (size_t)(lx->p - start);
    t.err_msg = NULL;
    return t;
}

static JToken jl_error_token(JLexer *lx, const char *start, size_t line, size_t col, const char *msg) {
    JToken t = jl_create_token(lx, JTK_ERROR, start, line, col);
    t.err_msg = msg;
    return t;
}

static void utf8_emit(uint32_t cp, char **out) {
    if (cp <= 0x7F) {
        *(*out)++ = (char)cp;
    } else if (cp <= 0x7FF) {
        *(*out)++ = (char)(0xC0 | (cp >> 6));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        *(*out)++ = (char)(0xE0 | (cp >> 12));
        *(*out)++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *(*out)++ = (char)(0xF0 | (cp >> 18));
        *(*out)++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *(*out)++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    }
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/* Decode a JSON string slice (without quotes) into malloc'd UTF-8. */
static char* json_decode_string(const char *s, size_t n, const char **err_msg) {
    size_t cap = n * 4 + 4;
    char *out = (char*)malloc(cap);
    if (!out) {
        if (err_msg) *err_msg = "oom";
        return NULL;
    }

    char *w = out;
    const char *end = s + n;

    while (s < end) {
        unsigned char c = (unsigned char)*s++;

        if (c == '\\') {
            if (s >= end) {
                if (err_msg) *err_msg = "trailing backslash";
                free(out);
                return NULL;
            }
            char esc = *s++;
            switch (esc) {
                case '"': *w++ = '"'; break;
                case '\\': *w++ = '\\'; break;
                case '/': *w++ = '/'; break;
                case 'b': *w++ = '\b'; break;
                case 'f': *w++ = '\f'; break;
                case 'n': *w++ = '\n'; break;
                case 'r': *w++ = '\r'; break;
                case 't': *w++ = '\t'; break;
                case 'u': {
                    if (end - s < 4) {
                        if (err_msg) *err_msg = "short \\u";
                        free(out);
                        return NULL;
                    }
                    int h0 = hex_val(s[0]), h1 = hex_val(s[1]), h2 = hex_val(s[2]), h3 = hex_val(s[3]);
                    if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) {
                        if (err_msg) *err_msg = "bad hex";
                        free(out);
                        return NULL;
                    }
                    uint32_t code = (uint32_t)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                    s += 4;

                    /* Surrogate pair */
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (end - s < 6 || s[0] != '\\' || s[1] != 'u') {
                            if (err_msg) *err_msg = "high surrogate without pair";
                            free(out);
                            return NULL;
                        }
                        s += 2;
                        int g0 = hex_val(s[0]), g1 = hex_val(s[1]), g2 = hex_val(s[2]), g3 = hex_val(s[3]);
                        if (g0 < 0 || g1 < 0 || g2 < 0 || g3 < 0) {
                            if (err_msg) *err_msg = "bad hex (low)";
                            free(out);
                            return NULL;
                        }
                        uint32_t low = (uint32_t)((g0 << 12) | (g1 << 8) | (g2 << 4) | g3);
                        s += 4;
                        if (low < 0xDC00 || low > 0xDFFF) {
                            if (err_msg) *err_msg = "invalid low surrogate";
                            free(out);
                            return NULL;
                        }
                        code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
                    }

                    utf8_emit(code, &w);
                } break;
                default:
                    if (err_msg) *err_msg = "bad escape";
                    free(out);
                    return NULL;
            }
        } else {
            /* Reject raw control chars */
            if (c < 0x20) {
                if (err_msg) *err_msg = "control char in string";
                free(out);
                return NULL;
            }
            *w++ = (char)c;
        }

        /* Grow output buffer if needed */
        if ((size_t)(w - out) + 4 >= cap) {
            size_t used = (size_t)(w - out);
            cap *= 2;
            char *nw = (char*)realloc(out, cap);
            if (!nw) {
                if (err_msg) *err_msg = "oom";
                free(out);
                return NULL;
            }
            out = nw;
            w = out + used;
        }
    }

    *w = '\0';
    return out;
}

static JToken jl_scan_string(JLexer *lx) {
    /* We are called after consuming the opening quote. */
    const char *start = lx->p - 1;
    size_t line = lx->line;
    size_t col  = lx->col - 1;

    while (!jl_at_end(lx)) {
        char c = jl_adv(lx);
        if (c == '"') {
            return jl_create_token(lx, JTK_STRING, start, line, col);
        }
        if ((unsigned char)c < 0x20) {
            return jl_error_token(lx, start, line, col, "control char in string");
        }
        if (c == '\\') {
            if (jl_at_end(lx)) return jl_error_token(lx, start, line, col, "trailing backslash");
            /* Skip next char (validation done in decode) */
            jl_adv(lx);
        }
    }

    return jl_error_token(lx, start, line, col, "unterminated string");
}

static bool jl_consume_kw(JLexer *lx, const char *kw) {
    while (*kw) {
        if (jl_peek(lx) != *kw) return false;
        jl_adv(lx);
        kw++;
    }
    return true;
}

static JToken jl_scan_number(JLexer *lx, char first) {
    const char *start = lx->p - 1;
    size_t line = lx->line;
    size_t col  = lx->col - 1;

    const char *p = start;
    const char *end = lx->end;

    /* Optional minus */
    if (*p == '-') {
        p++;
        if (p >= end || !isdigit((unsigned char)*p)) {
            lx->p = p;
            return jl_error_token(lx, start, line, col, "invalid number");
        }
    }

    /* Integer part (no leading zeros unless exactly 0) */
    if (p >= end || !isdigit((unsigned char)*p)) {
        lx->p = p;
        return jl_error_token(lx, start, line, col, "invalid number");
    }

    if (*p == '0') {
        p++;
        if (p < end && isdigit((unsigned char)*p)) {
            lx->p = p;
            return jl_error_token(lx, start, line, col, "leading zeros are not allowed");
        }
    } else {
        while (p < end && isdigit((unsigned char)*p)) p++;
    }

    /* Fraction */
    if (p < end && *p == '.') {
        p++;
        if (p >= end || !isdigit((unsigned char)*p)) {
            lx->p = p;
            return jl_error_token(lx, start, line, col, "invalid fraction");
        }
        while (p < end && isdigit((unsigned char)*p)) p++;
    }

    /* Exponent */
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        if (p >= end || !isdigit((unsigned char)*p)) {
            lx->p = p;
            return jl_error_token(lx, start, line, col, "invalid exponent");
        }
        while (p < end && isdigit((unsigned char)*p)) p++;
    }

    /* Commit */
    lx->p = p;
    lx->col = (size_t)(col + (lx->p - start));

    (void)first;
    return jl_create_token(lx, JTK_NUMBER, start, line, col);
}

/* ------------------------------
 * Exposed API
 * ------------------------------ */

bool jl_at_end(JLexer *lx) { return lx->p >= lx->end; }

char jl_peek(JLexer *lx) { return jl_at_end(lx) ? '\0' : *lx->p; }

char jl_adv(JLexer *lx) {
    if (jl_at_end(lx)) return '\0';
    char c = *lx->p++;
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

bool jl_match(JLexer *lx, char c) {
    if (jl_peek(lx) != c) return false;
    jl_adv(lx);
    return true;
}

void jl_init(JLexer *lx, const char *data, size_t len) {
    lx->buf = data;
    lx->p = data;
    lx->end = data + len;
    lx->line = 1;
    lx->col = 1;
}

void jl_skip_ws(JLexer *lx) {
    for (;;) {
        char c = jl_peek(lx);
        while (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            jl_adv(lx);
            c = jl_peek(lx);
        }
        return;
    }
}

JToken jl_next(JLexer *lx) {
    jl_skip_ws(lx);

    const char *start = lx->p;
    size_t line = lx->line;
    size_t col  = lx->col;

    if (jl_at_end(lx)) {
        return jl_create_token(lx, JTK_EOF, start, line, col);
    }

    char c = jl_adv(lx);

    switch (c) {
        case '{': return jl_create_token(lx, JTK_LBRACE, start, line, col);
        case '}': return jl_create_token(lx, JTK_RBRACE, start, line, col);
        case '[': return jl_create_token(lx, JTK_LBRACK, start, line, col);
        case ']': return jl_create_token(lx, JTK_RBRACK, start, line, col);
        case ':': return jl_create_token(lx, JTK_COLON, start, line, col);
        case ',': return jl_create_token(lx, JTK_COMMA, start, line, col);
        case '"': return jl_scan_string(lx);
        case 't':
            if (jl_consume_kw(lx, "rue")) return jl_create_token(lx, JTK_TRUE, start, line, col);
            return jl_error_token(lx, start, line, col, "unexpected token");
        case 'f':
            if (jl_consume_kw(lx, "alse")) return jl_create_token(lx, JTK_FALSE, start, line, col);
            return jl_error_token(lx, start, line, col, "unexpected token");
        case 'n':
            if (jl_consume_kw(lx, "ull")) return jl_create_token(lx, JTK_NULL, start, line, col);
            return jl_error_token(lx, start, line, col, "unexpected token");
        default:
            if (c == '-' || isdigit((unsigned char)c)) return jl_scan_number(lx, c);
            return jl_error_token(lx, start, line, col, "unexpected character");
    }
}

char* jl_string_to_utf8(const JToken *t, const char **err_msg_out) {
    if (err_msg_out) *err_msg_out = NULL;
    if (!t || t->type != JTK_STRING) {
        if (err_msg_out) *err_msg_out = "not a string token";
        return NULL;
    }
    if (t->length < 2) {
        if (err_msg_out) *err_msg_out = "short string token";
        return NULL;
    }

    /* Token includes quotes */
    const char *s = t->start + 1;
    size_t n = t->length - 2;
    const char *err = NULL;
    char *out = json_decode_string(s, n, &err);
    if (!out && err_msg_out) *err_msg_out = err;
    return out;
}

bool jl_number_to_double(const JToken *t, double *out) {
    if (!t || t->type != JTK_NUMBER || !out) return false;

    char *tmp = (char*)malloc(t->length + 1);
    if (!tmp) return false;
    memcpy(tmp, t->start, t->length);
    tmp[t->length] = '\0';

    errno = 0;
    char *endp = NULL;
    double v = strtod(tmp, &endp);
    bool ok = (errno == 0) && endp && (*endp == '\0');

    free(tmp);
    if (!ok) return false;
    *out = v;
    return true;
}

bool jl_number_to_int64(const JToken *t, int64_t *out) {
    if (!t || t->type != JTK_NUMBER || !out) return false;

    /* Reject if contains '.' or exponent */
    for (size_t i = 0; i < t->length; ++i) {
        char c = t->start[i];
        if (c == '.' || c == 'e' || c == 'E') return false;
    }

    char *tmp = (char*)malloc(t->length + 1);
    if (!tmp) return false;
    memcpy(tmp, t->start, t->length);
    tmp[t->length] = '\0';

    errno = 0;
    char *endp = NULL;
    long long v = strtoll(tmp, &endp, 10);
    bool ok = (errno == 0) && endp && (*endp == '\0');

    free(tmp);
    if (!ok) return false;
    *out = (int64_t)v;
    return true;
}