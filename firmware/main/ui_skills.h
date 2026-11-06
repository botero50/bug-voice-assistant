#ifndef UI_SKILLS_H
#define UI_SKILLS_H

#include "lvgl.h"

// Jokes skill
lv_obj_t *ui_create_jokes_screen(const char *setup, const char *punchline);

// Radio skill
lv_obj_t *ui_create_radio_screen(const char *station, const char *now_playing);

// Crypto skill
lv_obj_t *ui_create_crypto_screen(const char *price_btc, const char *price_eth);

// Book reader skill
lv_obj_t *ui_create_book_reader_screen(const char *title, const char *chapter, int page, int total_pages);

// Story generator skill
lv_obj_t *ui_create_story_generator_screen(const char *story_text);

#endif // UI_SKILLS_H
