#ifndef JUNO_H
#define JUNO_H

/* Public API for the Juno JSON parser (C).
 *
 * This header is intentionally small and does not expose lexer/tokenizer
 * internals. Internal headers live under src/internal/.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef JUNO_MAX_NESTING
#define JUNO_MAX_NESTING 64
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JND_ROOT_OBJ, /* legacy: kept for backward compatibility */
    JND_OBJ,
    JND_ARRAY,
    JND_STRING,
    JND_NUMBER,
    JND_BOOL,
    JND_NULL,
    JND_ERROR
} JNodeType;

typedef struct JsonNode {
    union {
        bool    bvalue;
        double  nvalue;
        int64_t ivalue;
        char   *svalue;
        char   *err_msg;
    } value;

    struct JsonNode *first_child;
    struct JsonNode *next_sibling;

    /* Only used if the node is a child of a JND_OBJ (property name). */
    char *key;

    JNodeType type;

    /* Valid only for JND_NUMBER: true => ivalue is set, false => nvalue is set */
    bool is_integer;
} JsonNode;

/* Parse JSON from a buffer (not necessarily NUL-terminated). */
JsonNode* juno_parse(const char *json_str, size_t len);

/* Parse JSON from file (loads whole file). Returns JND_ERROR on failure. */
JsonNode* juno_parse_file(const char *filename);

/* Free an AST returned by juno_parse / juno_parse_file (safe on NULL). */
void juno_free_ast(JsonNode *root);

/* Print the AST to stdout (safe on NULL). Used for debug. */
void juno_print_ast(JsonNode *root);

/* Convenience: check if a node is an error node. */
static inline bool juno_is_error(const JsonNode *node) {
    return node && node->type == JND_ERROR;
}

#ifdef __cplusplus
}
#endif

#endif /* JUNO_H */
