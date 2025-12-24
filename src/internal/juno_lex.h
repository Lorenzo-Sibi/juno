#ifndef JUNO_INTERNAL_LEX_H
#define JUNO_INTERNAL_LEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    JTK_EOF = 0,
    JTK_ERROR,
    JTK_LBRACE, JTK_RBRACE, JTK_LBRACK, JTK_RBRACK,
    JTK_COLON, JTK_COMMA,
    JTK_STRING, JTK_NUMBER, JTK_TRUE, JTK_FALSE, JTK_NULL
} JTokenType;

typedef struct {
    const char  *start;
    const char  *err_msg;
    const char  *line_start;
    const char  *buf_end;
    size_t       length;
    uint32_t     line, column;
    JTokenType   type;
} JToken;

typedef struct {
    const char *buf;
    const char *p;
    const char *end;
    const char *line_start;
    size_t line;
    size_t col;
} JLexer;

/* Low-level helpers used by the parser */
bool jl_at_end(JLexer *lx);
char jl_peek(JLexer *lx);
char jl_adv(JLexer *lx);
bool jl_match(JLexer *lx, char c);

void jl_init(JLexer *lx, const char *data, size_t len);
void jl_skip_ws(JLexer *lx);
JToken jl_next(JLexer *lx);

/* Token decoding helpers used by the parser */
char* jl_string_to_utf8(const JToken *t, const char **err_msg_out);
bool  jl_number_to_double(const JToken *t, double *out);
bool  jl_number_to_int64(const JToken *t, int64_t *out);

#endif
