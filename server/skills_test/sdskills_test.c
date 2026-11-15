// Host test of the skills-on-the-card file format (firmware/main/local_sdskills.c).
// Build and run:  cd server/skills_test && gcc -DSDSKILLS_HOST sdskills_test.c ../../firmware/main/local_sdskills.c ../../firmware/main/local_context.c -I../../firmware/main -o sdskills_test.exe && ./sdskills_test.exe
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "local_context.h"

bool sdskills_match(const char *file_text, const char *sentence, char *reply, size_t size, unsigned pick);

static int failed = 0, total = 0;

static const char *FILE_TEXT =
    "# my skills\r\n"
    "\r\n"
    "hola mundo | saluda a todos => Hola desde la tarjeta\r\n"
    "como esta mi perro => Está muy bien || Duerme todo el día\n"
    "que hora es en mi casa => Son las {hora}\n"
    "que dia es en mi casa => Hoy es {fecha}\n"
    "   espacios   =>   con espacios   \n"
    "sin flecha aqui\n"
    "vacia => \n"
    "ab => muy corta\n"
    "# comentado => no debe salir\n";

static void check(const char *said, const char *expected, unsigned pick) {
    char norm[200], reply[200] = "";
    local_context_normalize(said, norm, sizeof(norm));
    bool ok = sdskills_match(FILE_TEXT, norm, reply, sizeof(reply), pick);
    total++;
    if (expected == NULL ? ok : (!ok || strcmp(reply, expected) != 0)) {
        failed++;
        printf("FAIL: \"%s\" -> %s \"%s\" (expected %s)\n", said, ok ? "yes" : "no", reply, expected ? expected : "no match");
    }
}

int main(void) {
    check("Bug, ¡hola, mundo!", "Hola desde la tarjeta", 0);
    check("saluda a todos por favor", "Hola desde la tarjeta", 0);
    check("dile HOLA MUNDO a todos", "Hola desde la tarjeta", 0);
    check("cómo está mi perro", "Está muy bien", 0);
    check("cómo está mi perro", "Duerme todo el día", 1);
    check("cómo está mi perro", "Está muy bien", 2);  // wraps around
    check("espacios", "con espacios", 0);
    check("qué tal", NULL, 0);
    check("hola", NULL, 0);               // only part of a phrase
    check("sin flecha aqui", NULL, 0);    // no answer
    check("vacia", NULL, 0);              // empty answer
    check("ab", NULL, 0);                 // phrases under 3 letters are ignored
    check("comentado", NULL, 0);
    char norm[100], reply[200];
    local_context_normalize("qué hora es en mi casa", norm, sizeof(norm));
    total++;
    if (!sdskills_match(FILE_TEXT, norm, reply, sizeof(reply), 0) || strstr(reply, "{hora}") || strncmp(reply, "Son las ", 8) != 0) {
        failed++;
        printf("FAIL: {hora} not filled: \"%s\"\n", reply);
    }
    local_context_normalize("qué día es en mi casa", norm, sizeof(norm));
    total++;
    if (!sdskills_match(FILE_TEXT, norm, reply, sizeof(reply), 0) || strstr(reply, "{fecha}") || strncmp(reply, "Hoy es ", 7) != 0) {
        failed++;
        printf("FAIL: {fecha} not filled: \"%s\"\n", reply);
    }
    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}
