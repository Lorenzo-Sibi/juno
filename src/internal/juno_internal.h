#ifndef JUNO_INTERNAL_H
#define JUNO_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <juno/juno.h>
#include "juno_lex.h"

#ifndef JUNO_MAX_NESTING
#define JUNO_MAX_NESTING 64
#endif

/* Internal error handling (thread-unsafe!! Enhancement required TODO) */
void juno_error_set_msg(const char *err_msg);
JsonNode* juno_error(const char *err_msg);

/* Internal parser entry points */
JsonNode* juno_parse_obj(JLexer *lx, unsigned short depth);
JsonNode* juno_parse_array(JLexer *lx, unsigned short depth);
JsonNode* juno_parse_value(JLexer *lx, unsigned short depth);

#endif
