// "Who is speaking" by voice: "registra mi voz como María", "¿quién soy?", "¿a quién conoces?", "olvida mi voz".
// The voice comparison (voice_id.c) is small and honest: it greets people by name, it does not protect anything.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "assistant.h"
#include "local_skills_internal.h"
#include "voice_id.h"

static bool has(const char *text, const char *word) {
    char padded[48];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

// The name after `marker` in the sentence as it was heard (accents kept): up to two words, first letter in capitals
static bool name_after(const char *original, const char *marker, char *out, size_t size) {
    const char *p = original;
    size_t marker_length = strlen(marker);
    const char *found = NULL;
    for (; *p; p++) {
        if (strncasecmp(p, marker, marker_length) == 0 && (p == original || !isalpha((unsigned char)p[-1]))) found = p;
    }
    if (!found) return false;
    p = found + marker_length;
    while (*p == ' ') p++;
    size_t n = 0;
    int words = 0;
    while (*p && n + 1 < size && words < 2) {
        const unsigned char c = (unsigned char)*p;
        if (isalpha(c) || c >= 0x80) {
            bool capital = n == 0 || out[n - 1] == ' ';
            out[n++] = (char)(capital && c < 0x80 ? toupper(c) : c);
            p++;
        } else if (c == ' ' && n > 0 && isalpha((unsigned char)p[1])) {
            out[n++] = ' ';
            words++;
            p++;
        } else {
            break;
        }
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return n >= 2;
}

bool local_voiceid_try(const char *original, const char *text, local_answer_t *a) {
    bool about_voice = has(text, "voz") || has(text, "voces");
    char name[VID_NAME_MAX];

    // Learn a voice
    if (about_voice && (has(text, "registra") || has(text, "aprende") || has(text, "guarda") || has(text, "memoriza") || has(text, "recuerda") ||
                        has(text, "reconoce") || has(text, "entrena")) &&
        (name_after(original, "como ", name, sizeof(name)) || name_after(original, "soy ", name, sizeof(name)) ||
         name_after(original, "llamo ", name, sizeof(name)))) {
        if (voice_id_count() >= VID_MAX_PEOPLE) {
            bool known = false;
            for (int i = 0; i < voice_id_count(); i++) known |= strcasecmp(voice_id_name(i), name) == 0;
            if (!known) {
                ls_finish(a, "Ya conozco cuatro voces. Olvida una primero.", NULL);
                ls_speak(a, "Ya conozco cuatro voces. Olvida una primero.");
                return true;
            }
        }
        char screen[48], reply[96];
        snprintf(screen, sizeof(screen), "voiceid?n=%s", name);
        snprintf(reply, sizeof(reply), "Listo %s, dime tres frases largas.", name);
        ls_finish(a, reply, screen);
        ls_speak(a, reply);
        return true;
    }
    if (about_voice && (has(text, "registra") || has(text, "aprende") || has(text, "guarda") || has(text, "memoriza")) && !has(text, "olvida")) {
        const char *help = "Dime: registra mi voz como, y tu nombre.";
        ls_finish(a, help, NULL);
        ls_speak(a, help);
        return true;
    }

    // Forget
    if ((has(text, "olvida") || has(text, "borra") || has(text, "elimina")) && about_voice) {
        char reply[96];
        if (has(text, "todas") || has(text, "todos")) {
            voice_id_forget(NULL);
            snprintf(reply, sizeof(reply), "Listo, olvidé todas las voces.");
        } else if (name_after(original, "voz de ", name, sizeof(name)) || name_after(original, "a ", name, sizeof(name))) {
            snprintf(reply, sizeof(reply), voice_id_forget(name) ? "Listo, olvidé la voz de %s." : "No conozco a %s.", name);
        } else {
            char who[VID_NAME_MAX];
            float score = 0;
            if (assistant_current_speaker(who, sizeof(who), &score)) {
                voice_id_forget(who);
                snprintf(reply, sizeof(reply), "Listo %s, olvidé tu voz.", who);
            } else {
                snprintf(reply, sizeof(reply), "No sé quién eres. Dime: olvida la voz de, y el nombre.");
            }
        }
        ls_finish(a, reply, NULL);
        ls_speak(a, reply);
        return true;
    }

    // Who is it
    if (has(text, "quien soy") || has(text, "quien habla") || has(text, "quien te habla") || has(text, "me reconoces") || has(text, "sabes quien soy") ||
        has(text, "reconoces mi voz")) {
        char who[VID_NAME_MAX], reply[120];
        float score = 0;
        if (voice_id_count() == 0) {
            snprintf(reply, sizeof(reply), "Todavía no conozco ninguna voz. Di: registra mi voz como, y tu nombre.");
        } else if (assistant_current_speaker(who, sizeof(who), &score)) {
            snprintf(reply, sizeof(reply), "Creo que eres %s.", who);
        } else {
            snprintf(reply, sizeof(reply), "No estoy seguro de quién eres.");
        }
        ls_finish(a, reply, NULL);
        ls_speak(a, reply);
        return true;
    }

    // Who do you know
    if (has(text, "quienes conoces") || has(text, "a quien conoces") || has(text, "que voces conoces") || has(text, "quien esta registrado") ||
        has(text, "voces registradas")) {
        char reply[160] = "";
        int n = voice_id_count();
        if (n == 0) {
            snprintf(reply, sizeof(reply), "No conozco ninguna voz todavía.");
        } else {
            size_t used = (size_t)snprintf(reply, sizeof(reply), "Conozco a ");
            for (int i = 0; i < n && used < sizeof(reply) - 30; i++) {
                used += (size_t)snprintf(reply + used, sizeof(reply) - used, "%s%s", voice_id_name(i), i == n - 1 ? "." : i == n - 2 ? " y " : ", ");
            }
        }
        ls_finish(a, reply, NULL);
        ls_speak(a, reply);
        return true;
    }
    return false;
}
