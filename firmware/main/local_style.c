#include "local_skills_internal.h"
#include "settings_store.h"
#include "voice_clips.h"
#include <stdio.h>
#include <string.h>

// Changing how the Home screen looks, by voice: "cambia la interfaz" switches between the Lumina Kinetic look (the
// default) and the cobot telemetry look; "cambia los colores" or "pon el tema nórdico" changes the Lumina palette.

static bool has(const char *text, const char *word) {
    char padded[40];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

static bool any(const char *text, const char *const *words) {
    for (; *words; words++) {
        if (has(text, *words)) return true;
    }
    return false;
}

bool local_style_try(const char *text, local_answer_t *a) {
    static const char *const nouns[] = {"interfaz", "estilo", "diseno", "pantalla", "apariencia", "look", NULL};
    static const char *const color_nouns[] = {"tema", "colores", "color", "paleta", NULL};
    static const char *const change[] = {"cambia", "cambiar", "cambiame", "otra", "otro", "alterna", NULL};
    static const char *const put[] = {"pon", "poner", "ponme", "usa", "usar", "activa", "quiero", "cambia", "cambiar", "dame", NULL};
    static const char *const new_look[] = {"lumina", "cinetica", "cinetico", "kinetic", "nueva", "nuevo", "predeterminada", NULL};
    static const char *const old_look[] = {"clasica", "clasico", "telemetria", "cobot", "anterior", "vieja", "viejo", "antigua", "antiguo", NULL};
    static const char *const ceramic[] = {"ceramica", "terracota", "ocre", "naranja", "calido", "calida", NULL};
    static const char *const nordic[] = {"nordico", "nordica", "bosque", "verde", "esmeralda", NULL};
    static const char *const mono[] = {"mono", "monocromatico", "monocromo", "blanco", "negro", "eink", "tinta", NULL};

    bool about_look = any(text, nouns), about_color = any(text, color_nouns);
    if (!about_look && !about_color) return false;
    bool verb = any(text, put) || any(text, change);
    if (!verb) return false;

    int palette = -1;
    if (any(text, ceramic)) palette = 0;
    else if (any(text, nordic)) palette = 1;
    else if (any(text, mono)) palette = 2;

    int style = -1;
    if (any(text, new_look)) style = 0;
    else if (any(text, old_look)) style = 1;

    bug_settings_t now;
    settings_copy(&now);

    if (style >= 0 && about_look) {
        settings_set_ui_style((uint8_t)style);
        ls_say(a, CLIP_CAMBIE_INTERFAZ);
        ls_finish(a, "Listo, cambié la interfaz.", "home_reload");
        return true;
    }
    if (palette >= 0 && (about_color || about_look)) {
        settings_set_palette((uint8_t)palette);
        if (now.ui_style != 0) settings_set_ui_style(0);  // the palettes belong to the Lumina look
        ls_say(a, CLIP_CAMBIE_COLORES);
        ls_finish(a, "Listo, cambié los colores.", "home_reload");
        return true;
    }
    if (about_color && any(text, change)) {  // "cambia los colores": the next palette
        settings_set_palette((uint8_t)((now.palette + 1) % 3));
        if (now.ui_style != 0) settings_set_ui_style(0);
        ls_say(a, CLIP_CAMBIE_COLORES);
        ls_finish(a, "Listo, cambié los colores.", "home_reload");
        return true;
    }
    if (about_look && any(text, change)) {  // "cambia la interfaz": the other look
        settings_set_ui_style(now.ui_style == 0 ? 1 : 0);
        ls_say(a, CLIP_CAMBIE_INTERFAZ);
        ls_finish(a, "Listo, cambié la interfaz.", "home_reload");
        return true;
    }
    return false;
}
