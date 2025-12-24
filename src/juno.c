#include "internal/juno_internal.h"

/* ------------------------------
 * Internal error node
 * ------------------------------ */

static char g_error_buf[256] = "Error message not set";

static JsonNode g_error_node = {
    .value.err_msg = g_error_buf,
    .first_child   = NULL,
    .next_sibling  = NULL,
    .key           = NULL,
    .type          = JND_ERROR,
    .is_integer    = false,
};

void juno_error_set_msg(const char *err_msg) {
    if (!err_msg) return;
    /* Ensure NUL-termination */
    strncpy(g_error_buf, err_msg, sizeof(g_error_buf) - 1);
    g_error_buf[sizeof(g_error_buf) - 1] = '\0';
}

JsonNode* juno_error(const char *err_msg) {
    if (err_msg) juno_error_set_msg(err_msg);
    return &g_error_node;
}

/* ------------------------------
 * File helper
 * ------------------------------ */

static char* _read_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("open");
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *content = (char*)malloc((size_t)length + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t r = fread(content, 1, (size_t)length, file);
    if (r != (size_t)length) {
        if (ferror(file))
            fprintf(stderr, "Error reading file %s\n", filename);
        else
            fprintf(stderr, "Error: unexpected EOF reading file %s\n", filename);
        fclose(file);
        free(content);
        return NULL;
    }
    content[length] = '\0';
    fclose(file);
    return content;
}

/* ------------------------------
 * AST printing (debug)
 * ------------------------------ */

static inline const char* _nname(const JsonNode *node) {
    if (!node) return "NULL";
    switch (node->type) {
        case JND_ROOT_OBJ: return "ROOT";
        case JND_OBJ:      return "OBJ";
        case JND_ARRAY:    return "ARRAY";
        case JND_STRING:   return "STRING";
        case JND_NUMBER:   return "NUMBER";
        case JND_BOOL:     return "BOOL";
        case JND_NULL:     return "NULL";
        case JND_ERROR:    return "ERROR";
    }
    return "?";
}

static void _juno_print_ast_rec(JsonNode *node, unsigned depth) {
    if (!node) return;

    for (unsigned i = 0; i < depth; i++) printf("%s", "   ");
    printf("%s", (const char *)(depth ? ((node->next_sibling == NULL) ? "└─ " : "├─ ") : "└─ "));

    printf("%s", _nname(node));
    if (node->key) printf(" key=\"%s\"", node->key);

    switch (node->type) {
        case JND_STRING:
            printf(" : \"%s\"", node->value.svalue ? node->value.svalue : "");
            break;
        case JND_NUMBER:
            if (node->is_integer)
                printf(" : %lld", (long long)node->value.ivalue);
            else
                printf(" : %g", node->value.nvalue);
            break;
        case JND_BOOL:
            printf(" : %s", node->value.bvalue ? "true" : "false");
            break;
        case JND_NULL:
            printf(" : null");
            break;
        case JND_ERROR:
            printf(" : ERROR: %s", node->value.err_msg ? node->value.err_msg : "");
            break;
        default:
            break;
    }
    printf("\n");

    for (JsonNode *child = node->first_child; child; child = child->next_sibling) {
        _juno_print_ast_rec(child, depth + 1);
    }
}

void juno_print_ast(JsonNode *root) {
    _juno_print_ast_rec(root, 0);
}

/* ------------------------------
 * AST free
 * ------------------------------ */

void juno_free_ast(JsonNode *root) {
    if (!root) return;
    if (root == &g_error_node) return; /* static */

    JsonNode *child = root->first_child;
    while (child) {
        JsonNode *next = child->next_sibling;
        juno_free_ast(child);
        child = next;
    }

    if (root->type == JND_STRING && root->value.svalue) {
        free(root->value.svalue);
    }
    if (root->key) free(root->key);

    free(root);
}

/* ------------------------------
 * Internal node allocator
 * ------------------------------ */

static JsonNode* juno_create_node(JNodeType type) {
    JsonNode *node = (JsonNode*)calloc(1, sizeof(JsonNode));
    if (!node) return NULL;
    node->type = type;
    return node;
}

/* ------------------------------
 * Parsing internals
 * ------------------------------ */

JsonNode* juno_parse_value(JLexer *lx, unsigned short depth) {
    if (depth > JUNO_MAX_NESTING) return juno_error("maximum nesting reached");

    jl_skip_ws(lx);
    if (jl_peek(lx) == '{') return juno_parse_obj(lx, (unsigned short)(depth + 1));
    if (jl_peek(lx) == '[') return juno_parse_array(lx, (unsigned short)(depth + 1));

    JToken tok = jl_next(lx);

    switch (tok.type) {
        case JTK_STRING: {
            JsonNode *n = juno_create_node(JND_STRING);
            if (!n) return juno_error("oom (string)");
            n->value.svalue = jl_string_to_utf8(&tok, NULL);
            if (!n->value.svalue) {
                juno_free_ast(n);
                return juno_error(tok.err_msg ? tok.err_msg : "invalid string");
            }
            return n;
        }
        case JTK_NUMBER: {
            JsonNode *n = juno_create_node(JND_NUMBER);
            if (!n) return juno_error("oom (number)");
            int64_t iv = 0;
            if (jl_number_to_int64(&tok, &iv)) {
                n->is_integer = true;
                n->value.ivalue = iv;
            } else {
                n->is_integer = false;
                if (!jl_number_to_double(&tok, &n->value.nvalue)) {
                    juno_free_ast(n);
                    return juno_error("invalid number");
                }
            }
            return n;
        }
        case JTK_TRUE: {
            JsonNode *n = juno_create_node(JND_BOOL);
            if (!n) return juno_error("oom (bool)");
            n->value.bvalue = true;
            return n;
        }
        case JTK_FALSE: {
            JsonNode *n = juno_create_node(JND_BOOL);
            if (!n) return juno_error("oom (bool)");
            n->value.bvalue = false;
            return n;
        }
        case JTK_NULL:
            return juno_create_node(JND_NULL);
        case JTK_ERROR:
            return juno_error(tok.err_msg ? tok.err_msg : "lexer error");
        default:
            return juno_error("unexpected token while parsing value");
    }
}

JsonNode* juno_parse_array(JLexer *lx, unsigned short depth) {
    JsonNode *array = juno_create_node(JND_ARRAY);
    JsonNode *tail = NULL;

    if (!array) return juno_error("oom (array)");
    if (depth > JUNO_MAX_NESTING) {
        juno_error_set_msg("maximum nesting reached");
        goto error;
    }

    JToken tok = jl_next(lx);
    if (tok.type != JTK_LBRACK) {
        juno_error_set_msg("expected '[' while parsing array");
        goto error;
    }

    jl_skip_ws(lx);
    if (jl_peek(lx) == ']') { /* [] */
        (void)jl_next(lx);
        return array;
    }

    while (1) {
        JsonNode *val = juno_parse_value(lx, depth);
        if (!val || juno_is_error(val)) {
            juno_error_set_msg("error while parsing array element");
            goto error;
        }

        if (tail) tail->next_sibling = val;
        else array->first_child = val;
        tail = val;

        tok = jl_next(lx);
        if (tok.type == JTK_RBRACK) break;
        if (tok.type == JTK_COMMA) continue;

        juno_error_set_msg("expected ',' or ']' while parsing array");
        goto error;
    }

    return array;

error:
    juno_free_ast(array);
    return juno_error(NULL);
}

JsonNode* juno_parse_obj(JLexer *lx, unsigned short depth) {
    JsonNode *obj = juno_create_node(JND_OBJ);
    JsonNode *tail = NULL;

    if (!obj) return juno_error("oom (object)");
    if (depth > JUNO_MAX_NESTING) {
        juno_error_set_msg("maximum nesting reached");
        goto error;
    }

    JToken tok = jl_next(lx);
    if (tok.type != JTK_LBRACE) return juno_error("expected '{'");

    int first = 1;
    while (1) {
        tok = jl_next(lx);
        if (tok.type == JTK_RBRACE) break;

        if (!first) {
            if (tok.type != JTK_COMMA) {
                juno_error_set_msg("expected ',' between object properties");
                goto error;
            }
            tok = jl_next(lx);
        }
        first = 0;

        if (tok.type != JTK_STRING) {
            juno_error_set_msg("expected string as object key");
            goto error;
        }

        char *key = jl_string_to_utf8(&tok, NULL);
        if (!key) {
            juno_error_set_msg("invalid object key string");
            goto error;
        }

        tok = jl_next(lx);
        if (tok.type != JTK_COLON) {
            free(key);
            juno_error_set_msg("expected ':' after object key");
            goto error;
        }

        JsonNode *val = juno_parse_value(lx, (unsigned short)(depth + 1));
        if (!val || juno_is_error(val)) {
            free(key);
            juno_error_set_msg("error while parsing object value");
            goto error;
        }
        val->key = key;

        if (tail) tail->next_sibling = val;
        else obj->first_child = val;
        tail = val;
    }

    return obj;

error:
    juno_free_ast(obj);
    return juno_error(NULL);
}

/* ------------------------------
 * Public parsing entry points
 * ------------------------------ */

JsonNode* juno_parse(const char *json_str, size_t len) {
    if (!json_str) return juno_error("null input");

    JLexer lx;
    jl_init(&lx, json_str, len);

    jl_skip_ws(&lx);
    char c = jl_peek(&lx);

    /* We count nesting starting at 1 for the root container, so that
       depth == JUNO_MAX_NESTING is allowed and depth == JUNO_MAX_NESTING + 1 is rejected. */
    JsonNode *root = NULL;
    if (c == '{') root = juno_parse_obj(&lx, 1);
    else if (c == '[') root = juno_parse_array(&lx, 1);
    else root = juno_error("root must be object or array");

    return root;
}

JsonNode* juno_parse_file(const char *filename) {
    if (!filename) return juno_error("null filename");

    char *content = _read_file(filename);
    if (!content) return juno_error("failed to read file");

    JsonNode *root = juno_parse(content, strlen(content));
    free(content);
    return root;
}