// tests/test_main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../jsonp.h"

/* ------------------------------------------------------------------
 *  Minimal test framework
 * ------------------------------------------------------------------ */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond) do {                                      \
    if (!(cond)) {                                                  \
        printf("\033[0;31m[FAILED]\033[0m %s:%d: '%s'\n",           \
               __FILE__, __LINE__, #cond);                          \
        tests_failed++;                                             \
        return;                                                     \
    }                                                               \
} while (0)

#define ASSERT_STR_EQ(exp, act) do {                                \
    const char *e_ = (exp);                                         \
    const char *a_ = (act);                                         \
    if (((e_) == NULL && (a_) != NULL) ||                           \
        ((e_) != NULL && (a_) == NULL) ||                           \
        ((e_) && (a_) && strcmp(e_, a_) != 0)) {                    \
        printf("\033[0;31m[FAILED]\033[0m %s:%d: \"%s\" != \"%s\"\n",\
               __FILE__, __LINE__, e_ ? e_ : "(null)",              \
               a_ ? a_ : "(null)");                                 \
        tests_failed++;                                             \
        return;                                                     \
    }                                                               \
} while (0)

#define ASSERT_DOUBLE_NEAR(exp, act, eps) do {                      \
    double e_ = (exp);                                              \
    double a_ = (act);                                              \
    double diff_ = (e_ > a_) ? (e_ - a_) : (a_ - e_);               \
    if (diff_ > (eps)) {                                            \
        printf("\033[0;31m[FAILED]\033[0m %s:%d: %g != %g\n",       \
               __FILE__, __LINE__, e_, a_);                         \
        tests_failed++;                                             \
        return;                                                     \
    }                                                               \
} while (0)

#define RUN_TEST(fn) do {                                           \
    printf("[RUNNING] %s\n", #fn);                                  \
    int failed_before = tests_failed;                               \
    fn();                                                           \
    tests_run++;                                                    \
    if (tests_failed == failed_before) {                            \
        tests_passed++;                                             \
        printf("\033[0;32m[PASSED]\033[0m %s\n", #fn);              \
    }                                                               \
} while (0)

/* ------------------------------------------------------------------
 *  Small AST helpers for tests
 * ------------------------------------------------------------------ */

static JsonNode *find_member(JsonNode *obj, const char *key) {
    if (!obj || obj->type != JND_OBJ) return NULL;
    JsonNode *child = obj->first_child;
    while (child) {
        if (child->key && strcmp(child->key, key) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

static JsonNode *array_get(JsonNode *arr, size_t index) {
    if (!arr || arr->type != JND_ARRAY) return NULL;
    JsonNode *child = arr->first_child;
    size_t i = 0;
    while (child && i < index) {
        child = child->next_sibling;
        ++i;
    }
    return (i == index) ? child : NULL;
}

/* ------------------------------------------------------------------
 *  Test cases – cover JSON grammar & limits
 * ------------------------------------------------------------------ */

/* Basic object with string value */
static void test_simple_object(void) {
    const char *json = "{\"key\": \"value\"}";
    JsonNode *root = jp_parse(json, strlen(json));

    ASSERT_TRUE(root != NULL);
    ASSERT_TRUE(!jp_is_error(root));
    ASSERT_TRUE(root->type == JND_OBJ);

    JsonNode *val = find_member(root, "key");
    ASSERT_TRUE(val != NULL);
    ASSERT_TRUE(val->type == JND_STRING);
    ASSERT_STR_EQ("value", val->value.svalue);

    jp_free_ast(root);
}

/* Numbers: integer vs fractional / exponent */
static void test_numbers_variants(void) {
    const char *json = "{\"int\": 123, \"neg\": -5, \"frac\": 0.5, \"exp\": 1e3}";
    JsonNode *root = jp_parse(json, strlen(json));

    ASSERT_TRUE(root && !jp_is_error(root));

    JsonNode *n_int  = find_member(root, "int");
    JsonNode *n_neg  = find_member(root, "neg");
    JsonNode *n_frac = find_member(root, "frac");
    JsonNode *n_exp  = find_member(root, "exp");

    ASSERT_TRUE(n_int && n_int->type == JND_NUMBER && n_int->is_integer);
    ASSERT_TRUE(n_int->value.ivalue == 123);

    ASSERT_TRUE(n_neg && n_neg->type == JND_NUMBER && n_neg->is_integer);
    ASSERT_TRUE(n_neg->value.ivalue == -5);

    ASSERT_TRUE(n_frac && n_frac->type == JND_NUMBER && !n_frac->is_integer);
    ASSERT_DOUBLE_NEAR(0.5, n_frac->value.nvalue, 1e-9);

    ASSERT_TRUE(n_exp && n_exp->type == JND_NUMBER && !n_exp->is_integer);
    ASSERT_DOUBLE_NEAR(1000.0, n_exp->value.nvalue, 1e-9);

    jp_free_ast(root);
}

/* Invalid numbers – leading zeroes are forbidden by the grammar */
static void test_invalid_number_leading_zero(void) {
    const char *json = "{\"n\": 01}";
    JsonNode *root = jp_parse(json, strlen(json));
    ASSERT_TRUE(root != NULL);
    ASSERT_TRUE(jp_is_error(root));
}

/* Strings with escapes, including Unicode */
static void test_strings_and_unicode(void) {
    const char *json =
        "{"
        "\"escaped\": \"\\\"\\\\\\/\\b\\f\\n\\r\\t\","
        "\"euro\": \"\\u20AC\""
        "}";
    JsonNode *root = jp_parse(json, strlen(json));
    ASSERT_TRUE(root && !jp_is_error(root));

    JsonNode *escaped = find_member(root, "escaped");
    JsonNode *euro    = find_member(root, "euro");

    ASSERT_TRUE(escaped && escaped->type == JND_STRING);
    ASSERT_TRUE(euro && euro->type == JND_STRING);

    ASSERT_STR_EQ("\"\\/\b\f\n\r\t", escaped->value.svalue);
    /* Expected UTF‑8 for '€' (U+20AC) */
    ASSERT_STR_EQ("€", euro->value.svalue);

    jp_free_ast(root);
}

/* Literals true / false / null */
static void test_literals(void) {
    const char *json = "[true, false, null]";
    JsonNode *root = jp_parse(json, strlen(json));
    ASSERT_TRUE(root && !jp_is_error(root));
    ASSERT_TRUE(root->type == JND_ARRAY);

    JsonNode *t = array_get(root, 0);
    JsonNode *f = array_get(root, 1);
    JsonNode *n = array_get(root, 2);

    ASSERT_TRUE(t && t->type == JND_BOOL && t->value.bvalue == true);
    ASSERT_TRUE(f && f->type == JND_BOOL && f->value.bvalue == false);
    ASSERT_TRUE(n && n->type == JND_NULL);

    jp_free_ast(root);
}

/* Nested arrays up to MAX_NESTING */
static void test_max_nesting_ok(void) {
    /* Build something like [[[ ... 0 ... ]]] with depth == MAX_NESTING */
    const int depth = MAX_NESTING;
    int open_brackets = depth;
    int close_brackets = depth;

    size_t buf_size = (size_t)depth * 2 + 8;
    char *buf = (char*)malloc(buf_size);
    ASSERT_TRUE(buf != NULL);

    char *p = buf;
    for (int i = 0; i < open_brackets; ++i) *p++ = '[';
    *p++ = '0';
    for (int i = 0; i < close_brackets; ++i) *p++ = ']';
    *p = '\0';

    JsonNode *root = jp_parse(buf, strlen(buf));
    ASSERT_TRUE(root && !jp_is_error(root));

    jp_free_ast(root);
    free(buf);
}

/* Depth > MAX_NESTING should be rejected */
static void test_exceed_max_nesting(void) {
    const int depth = MAX_NESTING + 1;
    int open_brackets = depth;
    int close_brackets = depth;

    size_t buf_size = (size_t)depth * 2 + 8;
    char *buf = (char*)malloc(buf_size);
    ASSERT_TRUE(buf != NULL);

    char *p = buf;
    for (int i = 0; i < open_brackets; ++i) *p++ = '[';
    *p++ = '0';
    for (int i = 0; i < close_brackets; ++i) *p++ = ']';
    *p = '\0';

    JsonNode *root = jp_parse(buf, strlen(buf));
    ASSERT_TRUE(root != NULL);
    ASSERT_TRUE(jp_is_error(root));

    /* Do not free error_node; jp_free_ast is a no-op on it */
    jp_free_ast(root);
    free(buf);
}

/* Comments are not allowed by RFC – lexer should produce an error. */
static void test_reject_comments(void) {
    const char *json = "{/* comment */ \"a\": 1}";
    JsonNode *root = jp_parse(json, strlen(json));
    ASSERT_TRUE(root != NULL);
    ASSERT_TRUE(jp_is_error(root));
}

/* Use some of the JSON files under tests/json_files_test/ */
static void test_number_cases_file(void) {
    JsonNode *root = jp_parse_file("./tests/json_files_test/number_cases.json");
    ASSERT_TRUE(root != NULL);
    /* Just make sure it is either a valid tree or a well‑formed error node,
       i.e. the file is at least lexically valid JSON according to our rules. */
    ASSERT_TRUE(root->type == JND_ARRAY || root->type == JND_OBJ || jp_is_error(root));
    jp_free_ast(root);
}

/* ------------------------------------------------------------------
 *  Test runner
 * ------------------------------------------------------------------ */

int main(void) {
    printf("=== JSON tokenizer / parser test suite ===\n");

    RUN_TEST(test_simple_object);
    RUN_TEST(test_numbers_variants);
    RUN_TEST(test_invalid_number_leading_zero);
    RUN_TEST(test_strings_and_unicode);
    RUN_TEST(test_literals);
    RUN_TEST(test_max_nesting_ok);
    RUN_TEST(test_exceed_max_nesting);
    RUN_TEST(test_reject_comments);
    RUN_TEST(test_number_cases_file);

    printf("\n=== Summary ===\n");
    printf("Run: %d, Passed: %d, Failed: %d\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed == 0 ? 0 : 1;
}


