#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "text.h"

static void test_builtin_returns_string(void) {
    TextConfig cfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    TextProvider *tp = text_provider_create(&cfg);
    assert(tp != NULL);
    const char *s = text_get_next(tp);
    assert(s != NULL);
    assert(strlen(s) > 0);
    text_provider_destroy(tp);
    printf("PASS: test_builtin_returns_string\n");
}

static void test_builtin_never_null(void) {
    TextConfig cfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    TextProvider *tp = text_provider_create(&cfg);
    for (int i = 0; i < 20; i++) {
        const char *s = text_get_next(tp);
        assert(s != NULL && strlen(s) > 0);
    }
    text_provider_destroy(tp);
    printf("PASS: test_builtin_never_null\n");
}

static void test_file_source(void) {
    FILE *f = fopen("/tmp/noseguy_test.txt", "w");
    assert(f != NULL);
    fprintf(f, "Hello nose\n\nSecond paragraph\n");
    fclose(f);

    TextConfig cfg = { .source = TEXT_FILE, .arg = "/tmp/noseguy_test.txt",
                       .interval = 30.0 };
    TextProvider *tp = text_provider_create(&cfg);
    assert(tp != NULL);
    const char *s = text_get_next(tp);
    assert(s != NULL);
    assert(strstr(s, "Hello") != NULL);
    const char *s2 = text_get_next(tp);
    assert(s2 != NULL);
    assert(strstr(s2, "Second") != NULL);
    text_provider_destroy(tp);
    printf("PASS: test_file_source\n");
}

int main(void) {
    test_builtin_returns_string();
    test_builtin_never_null();
    test_file_source();
    printf("All text provider tests passed.\n");
    return 0;
}
