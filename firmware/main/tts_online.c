#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills.h"
#include "local_skills_internal.h"
#include "music_player.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A free online voice for text the board has no recording of (news, Wikipedia, jokes). The board asks Google Translate's
// text-to-speech service for an MP3 of each piece of the text (no key needed), decodes the MP3 itself and plays it.
// It needs internet; without it the text is only shown. The service takes short pieces, so the text is cut at the
// ends of sentences.

static const char *TAG = "TTS";

#define PIECE_BYTES 160

static void url_encode(const char *in, size_t length, char *out, size_t size) {
    size_t o = 0;
    for (size_t i = 0; i < length && o + 4 < size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') out[o++] = (char)c;
        else o += (size_t)snprintf(out + o, size - o, "%%%02X", c);
    }
    out[o] = '\0';
}

// Where to cut the text: the end of the last sentence (or comma, or word) that fits
static size_t cut_point(const char *text) {
    size_t length = strlen(text);
    if (length <= PIECE_BYTES) return length;
    size_t best_sentence = 0, best_comma = 0, best_space = 0;
    for (size_t i = 0; i < PIECE_BYTES; i++) {
        char c = text[i];
        if ((c == '.' || c == '!' || c == '?') && (text[i + 1] == ' ' || text[i + 1] == '\0')) best_sentence = i + 1;
        else if ((c == ',' || c == ';' || c == ':') && text[i + 1] == ' ') best_comma = i + 1;
        else if (c == ' ') best_space = i;
    }
    size_t cut = best_sentence > 60 ? best_sentence : best_comma > 60 ? best_comma : best_space > 20 ? best_space : PIECE_BYTES;
    while (cut > 0 && cut < length && ((unsigned char)text[cut] & 0xC0) == 0x80) cut--;  // not in the middle of a letter
    return cut;
}

bool tts_say_lang(const char *text, const char *lang) {
    bool played = false;
    char url[760];
    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        size_t cut = cut_point(p);
        if (cut == 0) break;
        char encoded[PIECE_BYTES * 3 + 8];
        url_encode(p, cut, encoded, sizeof(encoded));
        snprintf(url, sizeof(url), "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=%s&q=%s", lang && lang[0] ? lang : "es", encoded);
        size_t size = 0;
        char *mp3 = ls_http_get_len(url, true, &size);
        if (!mp3 || size < 200) {
            ESP_LOGW(TAG, "The online voice did not answer");
            heap_caps_free(mp3);
            break;
        }
        bool ok = music_play_mp3_memory((const uint8_t *)mp3, size);
        ESP_LOGI(TAG, "Online voice piece: %u bytes, %s", (unsigned)size, ok ? "played" : "could not be decoded");
        if (ok) played = true;
        heap_caps_free(mp3);
        p += cut;
    }
    return played;
}

bool tts_say(const char *text) {
    return tts_say_lang(text, "es");
}
