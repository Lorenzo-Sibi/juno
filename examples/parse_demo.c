#include <stdio.h>
#include <stdlib.h>

#include <juno/juno.h>

int main(int argc, char *argv[]) {
    const char *filename = (argc >= 2) ? argv[1] : "./tests/json_files_test/number_cases.json";

    JsonNode *root = juno_parse_file(filename);
    juno_print_ast(root);

    if (juno_is_error(root)){
        fprintf(stderr, "Parse error: %s\n", root->value.err_msg ? root->value.err_msg : "(unknown)");
        /* error node is static FOR NOW no free needed */
        return 1;
    } 

    juno_free_ast(root);
    return 0;
}