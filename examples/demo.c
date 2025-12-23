#include "jsonp.h"


int main(int argc, char *argv[]) {
    
    const char *filename = NULL;
    if (argc >= 2) filename = argv[1];
    else filename = "./tests/json_files_test/number_cases.json";

    char *json = _read_file(filename);
    if(!json) return 1;

    size_t json_len = strlen(json);

    if (argc >= 3 && (argv[2] == "-t" || argv[2] == "--tokenizer")) { // Lexing demo
        JLexer lx;
        int i = 0;
        jl_init(&lx, json, json_len);
        for(;;){
            JToken t = jl_next(&lx);
            i++;
            printf("%u:%u %-6s  '%.*s'\n", t.line, t.column, kname(t.type), (int)t.length, t.start);
            if (t.type == JTK_STRING){
                const char *err = NULL;
                char *s = jl_string_to_utf8(&t, &err);
                if (s){ printf("       decoded: \"%s\"\n", s); free(s); }
                else   printf("       decode error: %s\n", err ? err : "(unknown)");
            }
            if (t.type == JTK_NUMBER){ double v; if(jl_number_to_double(&t,&v)) printf("       number: %g\n", v); }
            if (t.type == JTK_ERROR){ fprintf(stderr, "ERROR: %s\n", t.err_msg ? t.err_msg : "token error"); break; }
            if (t.type == JTK_EOF) break;
        }
    }

    JsonNode *root = jp_parse(json, json_len);

    jp_print_ast(root);
    if (jp_is_error(root)) printf("Error message: %s\n", root->value.err_msg);

    jp_free_ast(root);
    free(json);
    return 0;
}