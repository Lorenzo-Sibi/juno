#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

#ifndef JSONT_H
#define JSONT_H

typedef enum {
    JTK_EOF = 0, JTK_ERROR,
    JTK_LBRACE, JTK_RBRACE, JTK_LBRACK, JTK_RBRACK,
    JTK_COLON, JTK_COMMA,
    JTK_STRING, JTK_NUMBER, JTK_TRUE, JTK_FALSE, JTK_NULL
} JTokenType;

typedef struct {
    const char  *start;
    const char *err_msg;
    size_t      length;
    uint32_t line, column;
    JTokenType type;
 } JToken;


typedef struct {
    const char *buf, *p, *end;
    size_t line, col;
} JLexer;


static inline bool jl_at_end(JLexer *lx);
static inline char jl_peek(JLexer *lx); // Returns the current char pointed by the lexer, '\0' otherwise
static inline char jl_adv(JLexer *lx);
static inline bool jl_match(JLexer *lx , char c); // Makes the lexer's pointer advance. Returns true if the current char pointed by the lexer matches the given char, false otherwise.

static JToken jl_create_token(JLexer *lx, JTokenType type, const char *start, size_t line, size_t col);
static JToken jl_error(JLexer *lx, const char *start, size_t line, size_t col, const char *msg);
static void jl_init(JLexer *lx, const char *data, size_t len);

static void jl_skip_ws(JLexer *lx);


// ---- UTF-8 helpers (for \uXXXX decoding)
static void utf8_emit(uint32_t cp, char **out);
static int hex_val(char c);


// Decode a STRING token to UTF-8 malloc'd buffer.
// Pass token that still includes the surrounding quotes.
static char* jl_string_to_utf8(const JToken *t, const char **err_msg_out);

// Decode a JSON string slice (without quotes) into a freshly malloc'd UTF-8 buffer.
// Returns NULL on error; sets *err_msg if provided.
static char* json_decode_string(const char *s, size_t n, const char **err_msg);

// Parser a JSON string token (including quotes), but doesn't allocate.
// Returns a token slice; decoding can be done separately with json_decode_string
static JToken jl_scan_string(JLexer *lx);

// Convert a NUMBER token to double (strict strtod on a temporary null-terminated copy).
static bool jl_number_to_double(const JToken *t, double *out);

// Try to convert a NUMBER token to a signed 64-bit integer.
// Returns true on success, false if the token is not a pure integer literal
// or if it is out of range for int64_t.
static bool jl_number_to_int64(const JToken *t, int64_t *out);

static JToken jl_scan_number(JLexer *lx, char first);

static bool jl_consume_kw(JLexer *lx, const char *kw);


static JToken jl_next(JLexer *lx);


static inline bool jl_at_end(JLexer *lx) { return lx->p >= lx->end; }
static inline char jl_peek(JLexer *lx) {return jl_at_end(lx) ? '\0' : *lx->p; }  // Returns the current char pointed by the lexer, '\0' otherwise

// Returns true if the current char pointed by the lexer matches the given char, false otherwise.
// Makes the lexer's pointer advance. 
static inline bool jl_match(JLexer *lx , char c) {
    if (jl_peek(lx) != c) return false; 
    jl_adv(lx); 
    return true; 
} 

static inline char jl_adv(JLexer *lx) {
    if (jl_at_end(lx)) return '\0';
    char c = *lx->p++;
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else lx->col++;
    return c;
}


static JToken jl_create_token(JLexer *lx, JTokenType type, const char *start, size_t line, size_t col) {
    JToken t = { 0 };
    t.type = type; 
    t.start = start;
    t.line = line; t.column = col;
    t.length = (size_t)(lx->p - start);
    t.err_msg = NULL;
    return t;
}

static JToken jl_error(JLexer *lx, const char *start, size_t line, size_t col, const char *msg) {
    JToken et = jl_create_token(lx, JTK_ERROR, start, line, col);
    et.err_msg = msg;
    return et;
}

static void jl_skip_ws(JLexer *lx){
    for(;;){
        char c = jl_peek(lx);
        while (c == ' ' || c == '\t' || c == '\r' || c == '\n'){ jl_adv(lx); c = jl_peek(lx); }
        // JSON has no comments—don’t accept them.
        return;
    }
}

static void jl_init(JLexer *lx, const char *data, size_t len){
    lx->buf = data; 
    lx->p = data; 
    lx->end = data + len; // end will point at the '\0'
    lx->line = 1; lx->col = 1;
}

// ---- UTF-8 helpers (for \uXXXX decoding)
static void utf8_emit(uint32_t cp, char **out){
    if (cp <= 0x7F) { *(*out)++ = (char)cp; }
    else if (cp <= 0x7FF) {
        *(*out)++ = (char)(0xC0 | (cp>>6));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        *(*out)++ = (char)(0xE0 | (cp>>12));
        *(*out)++ = (char)(0x80 | ((cp>>6)&0x3F));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *(*out)++ = (char)(0xF0 | (cp>>18));
        *(*out)++ = (char)(0x80 | ((cp>>12)&0x3F));
        *(*out)++ = (char)(0x80 | ((cp>>6)&0x3F));
        *(*out)++ = (char)(0x80 | (cp & 0x3F));
    }
}

static int hex_val(char c){
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return 10 + (c-'a');
    if (c>='A'&&c<='F') return 10 + (c-'A');
    return -1;
}

// Decode a JSON string slice (without quotes) into a freshly malloc'd UTF-8 buffer.
// Returns NULL on error; sets *err_msg if provided.
static char* json_decode_string(const char *s, size_t n, const char **err_msg){
    // Worst case: every char escaped -> output <= n. Surrogates expand but still < 4*n.
    size_t cap = n * 4 + 4;
    char *out = (char*)malloc(cap);
    if(!out) { if(err_msg) *err_msg = "oom"; return NULL; }
    char *w = out;
    const char *end = s + n;
    while (s < end){
        unsigned char c = (unsigned char)*s++;
        if (c == '\\'){
            if (s >= end) { if(err_msg) *err_msg = "trailing backslash"; free(out); return NULL; }
            char esc = *s++;
            switch (esc){
                case '"': *w++ = '"'; break;
                case '\\': *w++ = '\\'; break;
                case '/': *w++ = '/'; break;
                case 'b': *w++ = '\b'; break;
                case 'f': *w++ = '\f'; break;
                case 'n': *w++ = '\n'; break;
                case 'r': *w++ = '\r'; break;
                case 't': *w++ = '\t'; break;
                case 'u': {
                    if (end - s < 4){ if(err_msg)*err_msg="short \\u"; free(out); return NULL; }
                    int h0 = hex_val(s[0]), h1 = hex_val(s[1]), h2 = hex_val(s[2]), h3 = hex_val(s[3]);
                    if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0){ if(err_msg) *err_msg = "bad hex"; free(out); return NULL; }
                    uint32_t code = (h0 << 12) | (h1 << 8) | (h2 << 4) | h3;
                    s += 4;
                    // surrogate pair?
                    if (code >= 0xD800 && code <= 0xDBFF){
                        if (end - s < 6 || s[0]!='\\' || s[1]!='u'){ if(err_msg)*err_msg="high surrogate without pair"; free(out); return NULL; }
                        s += 2;
                        int g0=hex_val(s[0]), g1=hex_val(s[1]), g2=hex_val(s[2]), g3=hex_val(s[3]);
                        if (g0<0||g1<0||g2<0||g3<0){ if(err_msg)*err_msg="bad hex (low)"; free(out); return NULL; }
                        uint32_t low = (g0<<12)|(g1<<8)|(g2<<4)|g3;
                        s += 4;
                        if (low < 0xDC00 || low > 0xDFFF){ if(err_msg)*err_msg="invalid low surrogate"; free(out); return NULL; }
                        code = 0x10000 + (((code - 0xD800)<<10) | (low - 0xDC00));
                    }
                    utf8_emit(code, &w);
                } break;
                default: if(err_msg)*err_msg="bad escape"; free(out); return NULL;
            }
        } else {
            if (c < 0x20){ if(err_msg)*err_msg="control in string"; free(out); return NULL; }
            // pass through UTF-8 bytes as-is
            *w++ = (char)c;
        }
    }
    *w = '\0';
    return out;
}

// Parser a JSON string token (including quotes), but doesn't allocate.
// Returns a token slice; decoding can be done separately with json_decode_string
static JToken jl_scan_string(JLexer *lx) {
    const char *tok_start = lx->p-1;
    size_t line = lx->line, col = lx->col - 1;
    while(!jl_at_end(lx)) {
        char c = jl_adv(lx);
        if (c == '"') break;
        if ((unsigned char)c < 0x20) return jl_error(lx, tok_start, line, col, "control char in string");
        if (c == '\\') {
            if (jl_at_end(lx)) return jl_error(lx, tok_start, line, col, "truncated escape");
            char e = jl_adv(lx);
            if (e=='u'){
                // consume 4 hex (validated later if decoding)
                for(int i = 0; i < 4; i++)
                    if (!isxdigit((unsigned char)jl_adv(lx)))
                        return jl_error(lx, tok_start, line, col, "bad \\u");
            }
            // other escapes handled later
        }
    }
    if (*(lx->p-1) != '"') return jl_error(lx, tok_start, line, col, "unterminated string");
    return jl_create_token(lx, JTK_STRING, tok_start, line, col);
}

static JToken jl_scan_number(JLexer *lx, char first) {
    const char *start = lx->p-1;
    size_t line = lx->line, col = lx->col - 1;

    if (first == '-') {
        if (!isdigit((unsigned char) jl_peek(lx))) return jl_error(lx, start, line, col, "minus not followed by digit");

        if (jl_peek(lx) == '0') {
            jl_adv(lx);
            if(isdigit((unsigned char)jl_peek(lx))) return jl_error(lx, start, line, col, "leading zero in number (with negative sign)");
        } else {
            jl_adv(lx);
            while (isdigit((unsigned char )jl_peek(lx))) 
                jl_adv(lx);
        }
    } else {
        if (first == '0') {
            if(isdigit((unsigned char)jl_peek(lx))) return jl_error(lx, start, line, col, "leading zero in number");
        } else
            while (isdigit((unsigned char)jl_peek(lx))) jl_adv(lx); 
    }

    // fraction
    if (jl_match(lx, '.')) {
        if (!isdigit((unsigned char)jl_peek(lx))) return jl_error(lx, start, line, col, "dot not followed by digit");
        while (isdigit((unsigned char)jl_peek(lx))) 
            jl_adv(lx);
    }

    // exponent
    if (jl_peek(lx)=='e' || jl_peek(lx)=='E'){
        jl_adv(lx);
        if (jl_peek(lx)=='+' || jl_peek(lx)=='-') jl_adv(lx);
        if (!isdigit((unsigned char)jl_peek(lx))) return jl_error(lx, start, line, col, "bad exponent");
        while (isdigit((unsigned char)jl_peek(lx))) 
            jl_adv(lx);
    }

    return jl_create_token(lx, JTK_NUMBER, start, line, col);
}

static bool jl_consume_kw(JLexer *lx, const char *kw){
    for (size_t i=1; kw[i]; ++i){
        if (jl_at_end(lx) || jl_adv(lx) != kw[i]) return false;
    }
    return true;
}


JToken jl_next(JLexer *lx) {
    jl_skip_ws(lx);

    if (jl_at_end(lx)) {
        const char *s = lx->p;
        return jl_create_token(lx, JTK_EOF, s, lx->line, lx->col);
    }

    char c = jl_adv(lx);
    switch (c) {
        case '{': return jl_create_token(lx, JTK_LBRACE, lx->p-1, lx->line, lx->col-1);
        case '}': return jl_create_token(lx, JTK_RBRACE, lx->p-1, lx->line, lx->col-1);
        case '[': return jl_create_token(lx, JTK_LBRACK, lx->p-1, lx->line, lx->col-1);
        case ']': return jl_create_token(lx, JTK_RBRACK, lx->p-1, lx->line, lx->col-1);
        case ':': return jl_create_token(lx, JTK_COLON, lx->p-1, lx->line, lx->col-1);
        case ',': return jl_create_token(lx, JTK_COMMA, lx->p-1, lx->line, lx->col-1);

        // string
        case '"': return jl_scan_string(lx); break;
    
        // literals
        case 't': if (jl_consume_kw(lx, "true"))  return jl_create_token(lx, JTK_TRUE,  lx->p-4, lx->line, lx->col-4); break;
        case 'f': if (jl_consume_kw(lx, "false")) return jl_create_token(lx, JTK_FALSE, lx->p-5, lx->line, lx->col-5); break;
        case 'n': if (jl_consume_kw(lx, "null"))  return jl_create_token(lx, JTK_NULL,  lx->p-4, lx->line, lx->col-4); break;

        // number
        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return jl_scan_number(lx, c);
    }
    return jl_error(lx, lx->p-1, lx->line, lx->col-1, "unexpected character");
}

// Decode a STRING token to UTF-8 malloc'd buffer.
// Pass token that still includes the surrounding quotes.
static char* jl_string_to_utf8(const JToken *t, const char **err_msg_out){
    if (t->type != JTK_STRING || t->length < 2) { if(err_msg_out)*err_msg_out="not a string"; return NULL; }
    const char *s = t->start + 1;
    size_t n = t->length - 2;
    return json_decode_string(s, n, err_msg_out);
}

// Convert a NUMBER token to double (strict strtod on a temporary null-terminated copy).
static bool jl_number_to_double(const JToken *t, double *out){
    if (t->type != JTK_NUMBER) return false;
    char tmp[128];
    if (t->length < sizeof(tmp)) {
        memcpy(tmp, t->start, t->length);
        tmp[t->length] = 0;
        char *endptr = NULL;
        double v = strtod(tmp, &endptr);
        if (!endptr || *endptr != 0) return false;
        *out = v; 
        return true;
    }
    // rare: very long numbers
    char *buf = (char*)malloc(t->length + 1);
    if(!buf) return false;
    memcpy(buf, t->start, t->length); buf[t->length]=0;
    char *endptr=NULL; double v = strtod(buf, &endptr);
    bool ok = endptr && *endptr==0;
    free(buf);
    if (ok) *out = v;
    return ok;
}

// Helper: check if the numeric slice contains any fractional or exponent part.
// JSON numbers are decimal; if we see '.' or 'e'/'E' we know it's not an integer literal.
static bool jl_number_is_integral_slice(const char *s, size_t len){
    for (size_t i = 0; i < len; ++i){
        char c = s[i];
        if (c == '.' || c == 'e' || c == 'E')
            return false;
    }
    return true;
}

static bool jl_number_to_int64(const JToken *t, int64_t *out){
    if (t->type != JTK_NUMBER) return false;
    if (!t->start || t->length == 0) return false;

    // Fast reject if there is any fractional/exponent part.
    if (!jl_number_is_integral_slice(t->start, t->length))
        return false;

    // Copy to a temporary buffer to ensure null-termination.
    char buf[128];
    char *dyn_buf = NULL;
    char *num_str = NULL;
    if (t->length < sizeof(buf)) {
        memcpy(buf, t->start, t->length);
        buf[t->length] = '\0';
        num_str = buf;
    } else {
        dyn_buf = (char*)malloc(t->length + 1);
        if (!dyn_buf) return false;
        memcpy(dyn_buf, t->start, t->length);
        dyn_buf[t->length] = '\0';
        num_str = dyn_buf;
    }

    char *endptr = NULL;
    errno = 0;
    long long v = strtoll(num_str, &endptr, 10);
    bool ok = (endptr && *endptr == '\0' && errno == 0);
    if (dyn_buf) free(dyn_buf);
    if (!ok) return false;

    *out = (int64_t)v;
    return true;
}


static const char* kname(JTokenType k){
    switch(k){
        case JTK_EOF: return "EOF"; case JTK_ERROR: return "ERROR";
        case JTK_LBRACE: return "{"; case JTK_RBRACE: return "}";
        case JTK_LBRACK: return "["; case JTK_RBRACK: return "]";
        case JTK_COLON: return ":"; case JTK_COMMA: return ",";
        case JTK_STRING: return "STRING"; case JTK_NUMBER: return "NUMBER";
        case JTK_TRUE: return "TRUE"; case JTK_FALSE: return "FALSE"; case JTK_NULL: return "NULL";
    } return "?";
}

#endif

#ifdef JSON_LEXER_DEMO

int main(int argc, char**argv){
    const char *json = argc > 1 ? argv[1] : "{\"key\":[1,-2.5e+3,true,false,null,\"hi\\u20AC\"]}";
    JLexer lx; 
    jl_init(&lx, json, strlen(json));
    for(;;){
        JToken t = jl_next(&lx);
        printf("%zu:%zu %-6s  '%.*s'\n", t.line, t.column, kname(t.type), (int)t.length, t.start);
        if (t.type == JTK_STRING){
            const char *err=NULL; char *s = jl_string_to_utf8(&t, &err);
            if (s){ printf("       decoded: \"%s\"\n", s); free(s); }
            else   printf("       decode error: %s\n", err?err:"(unknown)");
        }
        if (t.type == JTK_NUMBER){ double v; if(jl_number_to_double(&t,&v)) printf("       number: %g\n", v); }
        if (t.type == JTK_ERROR){ fprintf(stderr, "ERROR: %s\n", t.err_msg ? t.err_msg : "token error"); break; }
        if (t.type == JTK_EOF) break;
    }
    return 0;
}
#endif