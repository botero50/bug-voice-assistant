// Host test of the filler cutting used before the skills read a sentence (firmware/main/local_skills.c).
// The functions are copied out of the firmware file at build time by the sed line below, so the test always checks the real code.
// Run it with:  bash scripts/run_host_tests.sh  (that script copies the functions out of the firmware file first, so this always tests the real code)
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "fillers_body.inc"  // made by scripts/run_host_tests.sh from firmware/main/local_skills.c

static int failed = 0, total = 0;
static void check(const char *in, const char *expected) {
    char text[200];
    snprintf(text, sizeof(text), " %s ", in);
    strip_fillers(text);
    char want[200];
    snprintf(want, sizeof(want), expected[0] ? " %s " : " ", expected);
    total++;
    if (strcmp(text, want) != 0) { failed++; printf("FAIL: \"%s\" -> \"%s\" (expected \"%s\")\n", in, text, want); }
}

int main(void) {
    check("que hora es", "que hora es");
    check("oye bug que hora es por favor", "que hora es");
    check("hola bug podrias decirme la hora", "la hora");
    check("por favor pon la radio", "pon la radio");
    check("pon la radio por favor ahora", "pon la radio");
    check("quiero saber que clima hace gracias", "que clima hace");
    check("que puedes hacer", "que puedes hacer");            // "puedes" in the middle stays
    check("puedes decirme la hora", "la hora");
    check("podrias decirme como esta el clima", "como esta el clima");
    check("pon salsa ya", "pon salsa");
    check("bug bug que dia es", "que dia es");
    check("por favor", "");
    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}
