#include "internal/juno_internal.h"

/* ------------------------------
 * Error handling
 * ------------------------------ */

static void _format_error(char *out, size_t out_len, const char *err_msg, const JToken *tok) {
    if (!out || out_len == 0) return;

    const char *reason = err_msg ? err_msg : JUNO_ERROR_MSG_DEFAULT;
    out[0] = '\0';

    if (!tok) {
        snprintf(out, out_len, "Parse error: %s", reason);
        return;
    }

    const char *line_start = tok->line_start;
    const char *buf_end = tok->buf_end;

    if (!line_start || !buf_end || line_start > tok->start || tok->start > buf_end) {
        snprintf(out, out_len, "Parse error at %u:%u: %s", tok->line, tok->column, reason);
        return;
    }

    size_t max_scan = (size_t)(buf_end - line_start);
    const char *nl = memchr(line_start, '\n', max_scan);
    const char *line_end = nl ? nl : buf_end;
    if (line_end < line_start) {
        snprintf(out, out_len, "Parse error at %u:%u: %s", tok->line, tok->column, reason);
        return;
    }

    size_t line_len = (size_t)(line_end - line_start);
    if (line_len == 0) {
        snprintf(out, out_len, "Parse error at %u:%u: %s", tok->line, tok->column, reason);
        return;
    }

    size_t col_idx = tok->column ? (size_t)(tok->column - 1) : 0;
    if (col_idx >= line_len) col_idx = line_len - 1;

    const size_t window = 70;
    size_t start_off = 0;
    if (line_len > window) {
        if (col_idx > window / 2) start_off = col_idx - (window / 2);
        if (start_off + window > line_len) start_off = line_len - window;
    }
    size_t snippet_len = line_len - start_off;
    if (snippet_len > window) snippet_len = window;

    size_t caret_pos = (col_idx > start_off) ? (col_idx - start_off) : 0;
    if (caret_pos >= snippet_len) caret_pos = snippet_len ? snippet_len - 1 : 0;

    snprintf(out, out_len,
        "Parse error at %u:%u\n  %.*s\n  %*s^\n  %s",
        tok->line, tok->column,
        (int)snippet_len, line_start + start_off,
        (int)caret_pos, "",
        reason
    );
}

void juno_error_set_msg(JsonNode *err_node, const char *err_msg) {
    if (!err_node || !err_msg || err_node->type != JND_ERROR) return;
    /* Ensure NUL-termination */
    char *err_msg_buf = (char*)calloc(1, JUNO_ERROR_MSG_MAX_LEN);
    strncpy(err_msg_buf, err_msg, JUNO_ERROR_MSG_MAX_LEN - 1);
    err_msg_buf[JUNO_ERROR_MSG_MAX_LEN - 1] = '\0';
    err_node->value.err_msg = err_msg_buf;
}

JsonNode* juno_error(const char *err_msg, const JToken *curr_tok) {
    JsonNode *err_node = (JsonNode*)calloc(1, sizeof(JsonNode));
    if (!err_node) return NULL;
    err_node->type = JND_ERROR;
    err_node->value.err_msg = JUNO_ERROR_MSG_DEFAULT;
    err_node->is_integer = false;

    char buf[JUNO_ERROR_MSG_MAX_LEN] = { 0 };
    if (curr_tok) {
        _format_error(buf, sizeof(buf), err_msg, curr_tok);
        juno_error_set_msg(err_node, buf);
    } else if (err_msg) juno_error_set_msg(err_node, err_msg);
    return err_node;
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

    JsonNode *child = root->first_child;
    while (child) {
        JsonNode *next = child->next_sibling;
        juno_free_ast(child);
        child = next;
    }

    if (root->type == JND_STRING && root->value.svalue) free(root->value.svalue);
    if (root->type == JND_ERROR && root->value.err_msg) free(root->value.err_msg);
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
    if (depth > JUNO_MAX_NESTING) return juno_error("maximum nesting reached", NULL);

    jl_skip_ws(lx);
    if (jl_peek(lx) == '{') return juno_parse_obj(lx, (unsigned short)(depth + 1));
    if (jl_peek(lx) == '[') return juno_parse_array(lx, (unsigned short)(depth + 1));

    JToken tok = jl_next(lx);

    switch (tok.type) {
        case JTK_STRING: {
            JsonNode *n = juno_create_node(JND_STRING);
            if (!n) return juno_error("oom (string)", &tok);
            n->value.svalue = jl_string_to_utf8(&tok, NULL);
            if (!n->value.svalue) {
                juno_free_ast(n);
                return juno_error(tok.err_msg ? tok.err_msg : "invalid string", &tok);
            }
            return n;
        }
        case JTK_NUMBER: {
            JsonNode *n = juno_create_node(JND_NUMBER);
            if (!n) return juno_error("oom (number)", &tok);
            int64_t iv = 0;
            if (jl_number_to_int64(&tok, &iv)) {
                n->is_integer = true;
                n->value.ivalue = iv;
            } else {
                n->is_integer = false;
                if (!jl_number_to_double(&tok, &n->value.nvalue)) {
                    juno_free_ast(n);
                    return juno_error("invalid number", &tok);
                }
            }
            return n;
        }
        case JTK_TRUE: {
            JsonNode *n = juno_create_node(JND_BOOL);
            if (!n) return juno_error("oom (bool)", &tok);
            n->value.bvalue = true;
            return n;
        }
        case JTK_FALSE: {
            JsonNode *n = juno_create_node(JND_BOOL);
            if (!n) return juno_error("oom (bool)", &tok);
            n->value.bvalue = false;
            return n;
        }
        case JTK_NULL:
            return juno_create_node(JND_NULL);
        case JTK_ERROR:
            return juno_error(tok.err_msg ? tok.err_msg : "lexer error", &tok);
        default:
            return juno_error("unexpected token while parsing value", &tok);
    }
}

JsonNode* juno_parse_array(JLexer *lx, unsigned short depth) {
    JsonNode *array = juno_create_node(JND_ARRAY);
    JsonNode *tail = NULL;
    char *err_msg = NULL;

    if (!array) return juno_error("oom (array)", NULL);
    if (depth > JUNO_MAX_NESTING) {
        err_msg = "maximum nesting reached";
        goto error;
    }

    JToken tok = jl_next(lx);
    if (tok.type != JTK_LBRACK) {
        err_msg = "expected '[' while parsing array";
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
            err_msg = "error while parsing array element";
            goto error;
        }

        if (tail) tail->next_sibling = val;
        else array->first_child = val;
        tail = val;

        tok = jl_next(lx);
        if (tok.type == JTK_RBRACK) break;
        if (tok.type == JTK_COMMA) continue;

        err_msg = "expected ',' or ']' while parsing array";
        goto error;
    }

    return array;

error:
    juno_free_ast(array);
    return juno_error(err_msg, &tok);
}

JsonNode* juno_parse_obj(JLexer *lx, unsigned short depth) {
    JsonNode *obj = juno_create_node(JND_OBJ);
    JsonNode *tail = NULL;
    char *err_msg = NULL;

    if (!obj) return juno_error("oom (object)", NULL);
    if (depth > JUNO_MAX_NESTING) {
        err_msg = "maximum nesting reached";
        goto error;
    }

    JToken tok = jl_next(lx);
    if (tok.type != JTK_LBRACE) return juno_error("expected '{'", &tok);

    int first = 1;
    while (1) {
        tok = jl_next(lx);
        if (tok.type == JTK_RBRACE) break;

        if (!first) {
            if (tok.type != JTK_COMMA) {
                err_msg = "expected ',' between object properties";
                goto error;
            }
            tok = jl_next(lx);
        }
        first = 0;

        if (tok.type != JTK_STRING) {
            err_msg = "expected string as object key";
            goto error;
        }

        char *key = jl_string_to_utf8(&tok, NULL);
        if (!key) {
            err_msg = "invalid object key string";
            goto error;
        }

        tok = jl_next(lx);
        if (tok.type != JTK_COLON) {
            free(key);
            err_msg = "expected ':' after object key";
            goto error;
        }

        JsonNode *val = juno_parse_value(lx, (unsigned short)(depth + 1));
        if (!val || juno_is_error(val)) {
            free(key);
            err_msg = "error while parsing object value";
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
    return juno_error(err_msg, &tok);

}

/* ------------------------------
 * Public parsing entry points
 * ------------------------------ */

JsonNode* juno_parse(const char *json_str, size_t len) {
    if (!json_str) return juno_error("null input", NULL);

    JLexer lx;
    jl_init(&lx, json_str, len);

    /* We count nesting starting at 1 for the root container, so that
       depth == JUNO_MAX_NESTING is allowed and depth == JUNO_MAX_NESTING + 1 is rejected. */
    JsonNode *root = juno_parse_value(&lx, 0);
    return root;
}

JsonNode* juno_parse_file(const char *filename) {
    if (!filename) return juno_error("null filename", NULL);

    char *content = _read_file(filename);
    if (!content) return juno_error("failed to read file", NULL);

    JsonNode *root = juno_parse(content, strlen(content));
    free(content);
    return root;
}
