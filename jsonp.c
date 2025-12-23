#include "jsonp.h"

JsonNode error_node = {
    .value.err_msg = "Error message not set",
    .first_child   = NULL,
    .next_sibling  = NULL,
    .key           = NULL,
    .type          = JND_ERROR,
    .is_integer    = false,
};

/* Internal Utils */

static inline const char* _nname(const JsonNode *node){
    if (!node) return "NULL";
    JNodeType k = node->type;
    switch(k){
        case JND_ROOT_OBJ: return "ROOT"; case JND_OBJ : return "OBJ"; case JND_ARRAY: return "ARRAY";
        case JND_STRING: return "STRING"; case JND_NUMBER: return "NUMBER"; case JND_ERROR: return "ERROR";
        case JND_BOOL: return "BOOL"; case JND_NULL: return "NULL";
    } return "?";
}

static inline char* _read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("open");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate memory and read
    char *content = malloc(length + 1);
    if (content) {
        unsigned int r = fread(content, 1, length, file);
        if(r <= 0) {
            if(ferror(file))
                fprintf(stderr, "Error reading file %s\n", filename);
            else
                fprintf(stderr, "Error: No data read from file %s\n", filename);
            fclose(file);
            free(content);
            return NULL;
        }
        content[length] = '\0';
    }
    fclose(file);
    return content;
}

/* Private APIs */

static void _jp_print_ast_rec(JsonNode *node, unsigned depth) {
    if (!node) return;
    for (unsigned i = 0; i < depth; i++) printf("%s", "   ");
    printf("%s", (const char *)(depth ? ((node->next_sibling == NULL) ? "└─ " : "├─ ") : "└─ ")); // branch connector

    // node label + value
    printf("%s", _nname(node));
    if (node->key) printf(" key=\"%s\"", node->key);
    switch (node->type) {
        case JND_STRING: printf(" : \"%s\"", node->value.svalue); break;
        case JND_NUMBER:
            if (node->is_integer)
                printf(" : %lld", (long long)node->value.ivalue);
            else
                printf(" : %g", node->value.nvalue);
            break;
        case JND_BOOL:   printf(" : %s", node->value.bvalue ? "true" : "false"); break;
        case JND_NULL:   printf(" : null"); break;
        case JND_ERROR:  printf(" : ERROR: %s", node->value.err_msg); break;
        default: break; // objects/arrays already described by type
    }
    printf("\n");

    // children
    JsonNode *child = node->first_child;
    while (child) {
        jp_print_ast_rec(child, depth + 1);
        child = child->next_sibling;
    }
}

static JsonNode* jp_create_node(JNodeType type) {
    JsonNode *node = (JsonNode*)calloc(1, sizeof(JsonNode));
    if (!node) return NULL;
    node->type = type;
    return node;
}

JsonNode* jp_parse_value(JLexer *lx, unsigned short int depth) {
    // I know it's ugly but it's fuctional :) (maybe I' gonna change it later ... or maybe no!)
    if (depth > MAX_NESTING) return jp_error("maximum nesting reached.");
    jl_skip_ws(lx);
    if (jl_peek(lx) == '{')
        return jp_parse_obj(lx, depth + 1);
    else if (jl_peek(lx) == '[')
        return jp_parse_array(lx, depth + 1);

    JToken tok = jl_next(lx);

    switch (tok.type) { // primitive values
        case JTK_STRING: {
            JsonNode *n = jp_create_node(JND_STRING);
            n->value.svalue = jl_string_to_utf8(&tok, NULL);
            return n;
        }
        case JTK_NUMBER: {
            JsonNode *n = jp_create_node(JND_NUMBER);
            int64_t iv = 0;
            // Prefer exact integer when possible, fall back to double for fractional/exp numbers.
            if (jl_number_to_int64(&tok, &iv)) {
                n->is_integer = true;
                n->value.ivalue = iv;
            } else {
                n->is_integer = false;
                jl_number_to_double(&tok, &n->value.nvalue);
            }
            return n;
        }
        case JTK_TRUE: {
            JsonNode *n = jp_create_node(JND_BOOL);
            n->value.bvalue = true;
            return n;
        }
        case JTK_FALSE: {
            JsonNode *n = jp_create_node(JND_BOOL);
            n->value.bvalue = false;
            return n;
        }
        case JTK_NULL: return jp_create_node(JND_NULL);
        default: return jp_error("Error invalue parsing."); // Error
    }
}

JsonNode* jp_parse_array(JLexer *lx, unsigned short int depth) {
    JsonNode *array = jp_create_node(JND_ARRAY);
    JsonNode *tail = NULL;

    if (!array) return jp_error("oom (array)");
    if (depth > MAX_NESTING) {
        jp_error_set_msg("maximum nesting reached.");
        goto error;
    }

    JToken tok = jl_next(lx);
    if (tok.type != JTK_LBRACK) {
        jp_error_set_msg("Error. Expected [' while array parsing.");
        goto error;
    }
    jl_skip_ws(lx);
    if (jl_peek(lx) == ']') { // trivial case "[]"
        jl_next(lx); // consume RBRACK
        return array;
    }
    while(1) {
        JsonNode *val = jp_parse_value(lx, depth);
        if (jp_is_error(val) || !val) {
            jp_error_set_msg("Error while parsing array.");
            goto error;
        }

        if (tail) tail->next_sibling = val;
        else array->first_child = val;
        tail = val;

        tok = jl_next(lx);
        if (tok.type == JTK_RBRACK) break;
        if (tok.type == JTK_COMMA) continue;
        jp_error_set_msg("Error while parsing array.");
        goto error;

    }
    return array;
error:
    jp_free_ast(array);
    return jp_error(NULL);
}

// TODO: find a way to handle the errors which should be compatible with the error handling in jp_parse
JsonNode* jp_parse_obj(JLexer *lx, unsigned short int depth) {
    JsonNode *obj = jp_create_node(JND_OBJ);
    JsonNode *tail = NULL;
    if (!obj) jp_error("oom (object parsing).");
    if (depth > MAX_NESTING) {
        jp_error_set_msg("maximum nesting reached.");
        goto error;
    }

    JToken tok = jl_next(lx);
    
    if (tok.type != JTK_LBRACE) return jp_error("Expected {");

    char *key = NULL;
    char fflag = 1;
    while(1) {
        tok = jl_next(lx);
        if (tok.type == JTK_RBRACE) break;
        if(!fflag) {
            if (tok.type != JTK_COMMA) {
                jp_error_set_msg("Error. Expected COMMA between properties.");
                goto error;
            }
            tok = jl_next(lx);
        }
        fflag = 0;
        
        if (tok.type != JTK_STRING) {
            jp_error_set_msg("Error. Expected STRING as property key.");
            goto error;
        }
        
        key = jl_string_to_utf8(&tok, NULL);
        
        tok = jl_next(lx);
        if (tok.type != JTK_COLON)
            goto error_k;
        
        JsonNode *val = jp_parse_value(lx, depth);
        if (jp_is_error(val) || !val)
            goto error_k;
        val->key = key;

        if(tail) 
            tail->next_sibling = val;
        else
            obj->first_child = val;
        tail = val;
    }
    return obj;
error_k:
    free(key);
error:
    jp_free_ast(obj);
    return jp_error(NULL);
}

/* Public APIs */

/**
 * \brief Parse a JSON string and return the root of the Abstract Sybtax Tree (AST).
 * 
 * This functions takes a JSON string and its length as input, parse the JSON data into an Abstract Syntax Tree (AST),
 * and returns the root node of the AST. If there is an error during parsing, an error node is returned.
 * 
 * \param json_str The JSON string to be parsed.
 * \param len The length (in bytes) of the JSON string.
 * \return A pointer to the root JsonNode of the AST, or an error node if an error occurs. 
 */
JsonNode* jp_parse(const char *json_str, size_t len) {
    JLexer lx;
    jl_init(&lx, json_str, len);

    int i = 0;
    for(;;){
        JToken t = jl_next(&lx);
        i++;
        if (t.type == JTK_ERROR) {
            fprintf(stderr, "ERROR: %s\n", t.err_msg ? t.err_msg : "token error");
            return jp_error("JSON validation error.");
        }
        if (t.type == JTK_EOF) break;
    }

    jl_init(&lx, json_str, len); // re-initialize lexer for parsing
    JsonNode *root = jp_parse_value(&lx, 0);
    return root;
}

/**
 * \brief Parse a JSON file and return the root of the Abstract Syntax Tree (AST).
 *
 * This function reads the contents of a JSON file specified by the given filename,
 * parses the JSON data into an Abstract Syntax Tree (AST), and returns the root node of the AST.
 * If the file cannot be read or if there is an error during parsing, an error node is returned.
 * 
 * \param filename The path to the JSON file to be parsed.
 * \return A pointer to the root JsonNode of the AST, or an error node if an error occurs.
 */
JsonNode* jp_parse_file(const char *filename) {
    if (!filename) return jp_error("Error. Filename not given.");
    char *json_str = _read_file(filename);
    if (!json_str) return jp_error("Error. Could not read file.");

    JsonNode *root = jp_parse(json_str, strlen(json_str));
    free(json_str);
    return root;
}

/**
 * \brief Print the Abstract Syntax Tree (AST) of a parsed JSON content.
 * 
 * \param root Pointer to the root JsonNode of the AST to be printed.
 */
void jp_print_ast(JsonNode *root) {
    if(!root) return;
    printf("\n\n=== JSON AST ===\n");
    _jp_print_ast_rec(root, 0);
}

/**
 * \brief Free the memory allocated for the JSON AST.
 * 
 * This function recursively frees the memory allocated for the JSON AST 
 * starting from the given the root node and all its children and sibilings. 
 * 
 * \param root Pointer to the root JsonNode of the AST to be freed.
 */
void jp_free_ast(JsonNode *root) {
    if (!root) return;
    if (jp_is_error(root)) return; // !! Do not free global error node
    JsonNode *child = root->first_child;
    while (child) {
        JsonNode *next = child->next_sibling;
        jp_free_ast(child);
        child = next;
    }
    #ifdef DEBUG
        printf("[DEBUG] Freeing %s node.\n", _nname(root));
    #endif
    if (root->type == JND_STRING && root->value.svalue) free(root->value.svalue);
    if (root->type == JND_ERROR && root->value.err_msg) free(root->value.err_msg);
    if (root->key) free(root->key);
    free(root);
}