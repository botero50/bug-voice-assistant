#include <stdio.h>
#include "local_skills.h"
int main(void) {
    const char *phrases[] = {
        "Agrega leche y huevos a la lista de compras", "Agrega pan a la lista de compras", "¿Qué hay en mi lista de compras?",
        "Quita la leche de la lista", "Agrega arroz, frijoles y café a mi lista", "Mis notas", "Anota que mañana hay reunión",
        "Anota que tengo que llamar a Ana", "Mis notas", "Borra las notas", "Borra la lista de compras", "Agrega a la lista de compras",
        "Quita el helado de la lista", "Pon un temporizador de 5 minutos", "Pon salsa", "Lista de compras", "Shopping list", "Notas", "Qué escuchas", NULL};
    for (int i = 0; phrases[i]; i++) {
        local_answer_t a;
        bool ok = local_skills_try(phrases[i], &a);
        printf("%-48s -> %s | %s | clips %d%s\n", phrases[i], ok ? "LOCAL" : "to PC ", ok ? a.reply : "", a.clip_count, a.json ? " +json" : "");
        if (a.json) printf("     %s\n", a.json);
    }
    return 0;
}
