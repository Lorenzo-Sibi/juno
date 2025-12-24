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

#ifndef JUNO_ERROR_MSG
#define JUNO_ERROR_MSG_MAX_LEN 128
#define JUNO_ERROR_MSG_DEFAULT "unspecified error"
#endif

void juno_error_set_msg(JsonNode *err_node, const char *err_msg);
JsonNode* juno_error(const char *err_msg, const JToken *curr_tok);

/* Internal parser entry points */
JsonNode* juno_parse_obj(JLexer *lx, unsigned short depth);
JsonNode* juno_parse_array(JLexer *lx, unsigned short depth);
JsonNode* juno_parse_value(JLexer *lx, unsigned short depth);

#endif