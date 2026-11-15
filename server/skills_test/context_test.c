// Host test of the follow-up rules (firmware/main/local_context.c).
// Build and run:  cd server/skills_test && gcc context_test.c ../../firmware/main/local_context.c -I../../firmware/main -o context_test.exe && ./context_test.exe
#include <stdio.h>
#include <string.h>
#include "local_context.h"

static int failed = 0, total = 0;

static void expand(const char *last, const char *said, const char *expected) {
    char out[200] = "";
    bool ok = local_context_expand(said, last, out, sizeof(out));
    total++;
    if (expected == NULL ? ok : (!ok || strcmp(out, expected) != 0)) {
        failed++;
        printf("FAIL: after \"%s\", \"%s\" -> %s \"%s\" (expected %s)\n", last, said, ok ? "yes" : "no", out, expected ? expected : "no change");
    }
}

static void repeat(const char *said, bool expected) {
    total++;
    if (local_context_is_repeat(said) != expected) {
        failed++;
        printf("FAIL: repeat(\"%s\") should be %s\n", said, expected ? "true" : "false");
    }
}

int main(void) {
    // weather
    expand("como esta el clima hoy", "y mañana", "cómo estará el clima mañana");
    expand("cómo está el clima hoy", "Y pasado mañana", "cómo estará el clima pasado mañana");
    expand("cómo está el clima", "y en Bogotá?", "cómo está el clima en Bogotá?");
    expand("va a llover", "y en Cali", "cómo está el clima en Cali");
    // prices
    expand("cuanto vale el dolar", "y el euro", "cuánto vale el euro");
    expand("cuánto vale el bitcoin", "y el euro", "cuánto vale el euro");
    expand("cuanto vale el dolar", "y bitcoin", "cuánto vale el bitcoin");
    expand("cuanto vale el dolar", "y mañana", NULL);  // a day is not a currency
    // people and things
    expand("quién es Shakira", "y Karol G", "quién es Karol G");
    expand("qué es un volcán", "y un terremoto", "qué es un terremoto");
    // not follow-ups
    expand("cómo está el clima", "qué hora es", NULL);
    expand("cómo está el clima", "pon música", NULL);
    expand("", "y mañana", NULL);
    expand("pon salsa", "y mañana", NULL);
    expand("cómo está el clima", "y dime todo lo que sabes sobre la historia completa del imperio romano y sus emperadores", NULL);
    // repeat
    repeat("repite lo último", true);
    repeat("repite", true);
    repeat("Qué dijiste", true);
    repeat("otra vez", true);
    repeat("cómo está el clima", false);  // contains "como" but is a new question
    repeat("pon música", false);
    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}
