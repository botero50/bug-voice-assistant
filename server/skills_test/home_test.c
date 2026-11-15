// Host test of the smart-home phrases and the devices file (firmware/main/local_home.c).
// Build and run:  cd server/skills_test && gcc -DHOME_HOST home_test.c ../../firmware/main/local_home.c ../../firmware/main/local_context.c -I../../firmware/main -o home_test.exe && ./home_test.exe
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "local_context.h"

typedef struct { char name[64]; char command[96]; char state[96]; } home_device_t;
typedef enum { HOME_NONE, HOME_ON, HOME_OFF, HOME_STATE } home_action_t;
home_action_t home_parse(const char *sentence);
bool home_find_device(const char *file_text, const char *sentence, home_device_t *device);

static int failed = 0, total = 0;
static void check(const char *name, int ok) { total++; if (!ok) { failed++; printf("FAIL: %s\n", name); } }

static home_action_t act(const char *said) { char n[200]; local_context_normalize(said, n, sizeof(n)); return home_parse(n); }
static bool find(const char *said, home_device_t *d) {
    static const char *file =
        "# my home\r\n"
        "luz de la sala | lámpara de la sala => casa/sala/luz/set | casa/sala/luz/state\r\n"
        "ventilador => casa/cuarto/ventilador/set\n"
        "luz del cuarto => casa/cuarto/luz/set | casa/cuarto/luz/state\n"
        "sin flecha\n";
    char n[200];
    local_context_normalize(said, n, sizeof(n));
    return home_find_device(file, n, d);
}

int main(void) {
    check("enciende", act("Enciende la luz de la sala") == HOME_ON);
    check("prende", act("prende el ventilador por favor") == HOME_ON);
    check("apaga", act("Bug, apaga la luz del cuarto") == HOME_OFF);
    check("¿está encendida?", act("¿Está encendida la luz de la sala?") == HOME_STATE);
    check("¿está apagado?", act("está apagado el ventilador") == HOME_STATE);
    check("estado de", act("dime el estado de la luz del cuarto") == HOME_STATE);
    check("nothing", act("qué hora es") == HOME_NONE);
    check("state wins over on", act("está prendida la luz") == HOME_STATE);

    home_device_t d;
    check("finds the first name", find("enciende la luz de la sala", &d) && strcmp(d.name, "luz de la sala") == 0 &&
          strcmp(d.command, "casa/sala/luz/set") == 0 && strcmp(d.state, "casa/sala/luz/state") == 0);
    check("finds an alias", find("apaga la lámpara de la sala", &d) && strcmp(d.command, "casa/sala/luz/set") == 0);
    check("a device with no state topic", find("prende el ventilador", &d) && strcmp(d.command, "casa/cuarto/ventilador/set") == 0 && d.state[0] == '\0');
    check("the other light", find("apaga la luz del cuarto", &d) && strcmp(d.command, "casa/cuarto/luz/set") == 0);
    check("an unknown device is not touched", !find("apaga la luz del baño", &d));
    check("a sentence about something else", !find("pon música", &d));
    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}
