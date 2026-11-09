#include "dev_bridge.h"
#include "local_skills_internal.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

// Claude Code from the board, by voice (needs the dev bridge paired, see docs/DEV_BRIDGE_PLAN.md):
//   "aprueba" / "rechaza"          answer the permission Claude is asking for (also "sí" / "no" while one is open)
//   "cómo va Claude"               what the Claude sessions on the PC are doing
//   "dile a Claude que continúe"   resumes the idle session; anything after "que" is sent as the instruction
// The PC does the work and the board says the result when it arrives, so these answers are short.

static bool has(const char *text, const char *word) {
    char padded[48];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

static bool any(const char *text, const char *const *words) {
    for (; *words; words++) {
        if (has(text, *words)) return true;
    }
    return false;
}

static bool answer(local_answer_t *a, const char *spoken) {
    ls_speak(a, spoken);
    ls_finish(a, spoken, NULL);
    return true;
}

bool local_claude_try(const char *text, local_answer_t *a) {
    static const char *const yes[] = {"aprueba", "apruebalo", "aprobar", "aprobado", "autoriza", "autorizalo", "permitelo", "permite", "dale permiso", NULL};
    static const char *const no[] = {"rechaza", "rechazalo", "rechazar", "niega", "negalo", "denegar", "deniega", "cancelalo", NULL};
    static const char *const short_yes[] = {"si", "dale", "adelante", "ok", "claro", NULL};
    static const char *const short_no[] = {"no", "nada", "para", "detente", NULL};

    int pending = dev_bridge_pending_ask();
    bool mentions_claude = has(text, "claude") || has(text, "clod") || has(text, "cloud");
    bool negated = strstr(text, " no lo ") != NULL;  // "no lo apruebes"
    bool approve = any(text, yes) && !negated, reject = any(text, no) || (any(text, yes) && negated);
    bool short_text = strlen(text) < 22;
    if (pending > 0 && !approve && !reject && short_text) {
        approve = any(text, short_yes);
        reject = any(text, short_no);
    }
    if (approve != reject) {  // exactly one of them
        if (!dev_bridge_paired()) return false;
        if (pending <= 0) return answer(a, "No hay ningún permiso pendiente.");
        dev_bridge_decide(pending, approve);
        return answer(a, approve ? "Aprobado." : "Rechazado.");
    }

    // "toma un pantallazo" / "captura de mi pantalla": a small picture of the PC screen appears on the board (the full one is saved on the PC)
    if (dev_bridge_paired() && !mentions_claude &&
        (has(text, "pantallazo") || has(text, "screenshot") || strstr(text, " muestrame mi pantalla ") || strstr(text, " ensename mi pantalla ") || strstr(text, " ver mi pantalla ") ||
         strstr(text, " ensename el escritorio ") || strstr(text, " muestrame el escritorio ") || strstr(text, " como va mi pantalla ") || strstr(text, " mira mi pantalla ") || strstr(text, " captura de pantalla ") || strstr(text, " captura de mi pantalla ") ||
         strstr(text, " foto de mi pantalla ") || strstr(text, " foto a mi pantalla "))) {
        dev_bridge_send("shot", 0, false, NULL);
        return answer(a, "Voy a tomarlo.");
    }

    // "pregúntale a Claude por qué falló el login": a read-only question; "y el siguiente" / "y por qué" within five minutes continues it
    static int64_t ask_at_ms = 0;
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool follow_up = dev_bridge_paired() && ask_at_ms && now_ms - ask_at_ms < 300000 && strncmp(text, " y ", 3) == 0 &&
                     (has(text, "siguiente") || strstr(text, " por que ") || has(text, "explica") || has(text, "cual") || has(text, "como") || has(text, "donde") ||
                      has(text, "mas") || has(text, "eso") || has(text, "ese") || has(text, "esa"));
    const char *who = strstr(text, "claude");
    if (follow_up || (mentions_claude && dev_bridge_paired() && (strstr(text, " pregunta ") || strstr(text, " preguntale ") || strstr(text, " consultale ") ||
                                                                   strstr(text, " pregunta a claude ")) && who)) {
        char question[200];
        const char *from = follow_up ? text + 3 : (who + 6);  // after "y " or after "claude"
        while (*from == ' ') from++;
        strlcpy(question, from, sizeof(question));
        size_t n = strlen(question);
        while (n > 0 && question[n - 1] == ' ') question[--n] = 0;
        if (!question[0]) return answer(a, "¿Qué quieres preguntarle a Claude?");
        dev_bridge_send("ask", 0, false, question);
        ask_at_ms = now_ms;
        return answer(a, "Se lo pregunto a Claude.");
    }

    if (!mentions_claude) return false;
    if (!dev_bridge_paired()) return answer(a, "Todavía no estoy conectado a tu computador. Hazlo en la página de instalación, con el token.");

    if (strstr(text, " que hizo ") || strstr(text, " que hiciste ") || strstr(text, " que hicieron ") || strstr(text, " ultima respuesta ") ||
        strstr(text, " repite ") || strstr(text, " explicame ") || strstr(text, " resumen ")) {
        dev_bridge_send("last", 0, false, NULL);
        return answer(a, "Te muestro lo último que dijo Claude.");
    }

    if (strstr(text, " como va ") || strstr(text, " como vas con ") || strstr(text, " que esta haciendo ") || strstr(text, " estado ") ||
        strstr(text, " ya termino ") || strstr(text, " como esta ")) {
        dev_bridge_send("status", 0, false, NULL);
        return answer(a, "Voy a preguntarle.");
    }

    // "dile a Claude que revise las pruebas", "claude continua", "que claude continue"
    const char *after = strstr(text, " que ");
    const char *claude = strstr(text, "claude");
    const char *instruction = NULL;
    if (strstr(text, " dile ") || strstr(text, " pidele ") || strstr(text, " ordenale ") || strstr(text, " manda ") || strstr(text, " envia ")) {
        if (after && claude && after > claude) instruction = after + 5;
    }
    char order[160] = "continúa";
    if (instruction && instruction[0]) {
        strlcpy(order, instruction, sizeof(order));
        size_t n = strlen(order);
        while (n > 0 && order[n - 1] == ' ') order[--n] = '\0';
    } else if (!(has(text, "continua") || has(text, "continue") || has(text, "sigue") || has(text, "siga") || has(text, "adelante"))) {
        return false;  // mentions Claude but asks nothing for the PC: leave it to the other skills
    }
    dev_bridge_send("say", 0, false, order);
    return answer(a, "Listo, se lo digo a Claude.");
}
