#ifndef LOCAL_SKILLS_INTERNAL_H
#define LOCAL_SKILLS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "local_skills.h"

// Shared by local_skills.c and local_timers.c
void ls_say(local_answer_t *a, int clip);
void ls_say_number(local_answer_t *a, int n);  // 0 to 100 as words
void ls_finish(local_answer_t *a, const char *reply, const char *screen);

// The shopping list and the notes; `original` is the sentence as heard (accents and capitals kept)
bool local_lists_try(const char *original, local_answer_t *a);

// Colombian holidays (computed) and the weather (fetched by the board from Open-Meteo); `text` is normalized
bool local_holidays_try(const char *text, local_answer_t *a);
bool local_weather_try(const char *text, local_answer_t *a);

// Math, unit conversions and prices (the prices come from free online services the board calls itself)
bool local_math_try(const char *text, local_answer_t *a);
bool local_prices_try(const char *text, local_answer_t *a);

// Radio and music (the board plays MP3 itself); `original` keeps the accents that were heard
bool local_radio_try(const char *original, const char *text, local_answer_t *a);

// Look of the Home screen: interface and colours, by voice
bool local_style_try(const char *text, local_answer_t *a);

// The riddles of the game "Adivinanzas" (games_words.c), told by voice too
int riddle_count(void);
const char *riddle_text(int i);
const char *riddle_answer(int i);
const char *riddle_keyword(int i);

// Jokes, news and Wikipedia (said with the free online voice)
bool local_online_try(const char *original, const char *text, local_answer_t *a);

// Email read by the board over IMAP
bool local_mail_try(const char *text, local_answer_t *a);

// The agenda (ICS links read by the board itself)
bool local_calendar_try(const char *text, local_answer_t *a);
bool local_calendar_configured(void);
void local_calendar_set_urls(const char *list);  // links separated by ';'

// GET a small text body (heap, PSRAM; the caller frees it); NULL if there is no answer
char *ls_http_get(const char *url, bool secure);
char *ls_http_get_len(const char *url, bool secure, size_t *length);  // the same, also gives the size (for binary data)

// Translation (MyMemory, free), the article screen's data, and short spoken summaries other skills share
bool ls_translate(const char *from, const char *to, const char *text, char *out, size_t size);
char *ls_article_json(const char *title, const char *text, const char *footer);
bool local_weather_brief(char *out, size_t size);
bool local_calendar_brief(char *out, size_t size);
// Translation, favourite stations, books from the card and the morning briefing
bool local_more_try(const char *original, const char *text, local_answer_t *a);
void ls_speak_lang(local_answer_t *a, const char *text, const char *lang);

// Say this text with the free online voice, after the clips (the board has no recording of it)
void ls_speak(local_answer_t *a, const char *text);
// A whole number up to 999,999,999 as words
void ls_say_big(local_answer_t *a, long long n);

// `text` is already normalized (lowercase, no accents or punctuation, one space between words, spaces at both ends)
bool local_timers_try(const char *text, local_answer_t *a);

#endif
