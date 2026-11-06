#include <stdio.h>
#include "ui_skills.h"
#include "ui_config.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_SKILLS";

// ==================== JOKES SKILL ====================

lv_obj_t *ui_create_jokes_screen(const char *setup, const char *punchline) {
    ESP_LOGI(TAG, "Creating jokes screen");

    lv_obj_t *root = lv_scr_act();

    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 350);
    lv_obj_set_pos(container, 33, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header
    lv_obj_t *header = lv_label_create(container);
    lv_label_set_text(header, "😄 Joke of the Day");
    lv_obj_set_pos(header, 5, 5);
    lv_obj_set_style_text_color(header, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

    // Joke card
    lv_obj_t *card = lv_obj_create(container);
    lv_obj_set_size(card, 380, 200);
    lv_obj_set_pos(card, 10, 40);
    lv_obj_set_style_bg_color(card, COLOR_ACCENT_GRAY, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_border_color(card, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    // Setup text
    lv_obj_t *setup_label = lv_label_create(card);
    lv_label_set_text(setup_label, setup ? setup : "Why did the CLI go to school?");
    lv_obj_set_width(setup_label, 340);
    lv_label_set_long_mode(setup_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(setup_label, 0, 10);
    lv_obj_set_style_text_color(setup_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(setup_label, &lv_font_montserrat_14, 0);

    // Divider
    lv_obj_t *divider = lv_obj_create(card);
    lv_obj_set_size(divider, 340, 1);
    lv_obj_set_pos(divider, 0, 80);
    lv_obj_set_style_bg_color(divider, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_20, 0);
    lv_obj_set_style_border_opa(divider, LV_OPA_0, 0);

    // Punchline text
    lv_obj_t *punchline_label = lv_label_create(card);
    lv_label_set_text(punchline_label, punchline ? punchline : "To improve its \"class\" structure!");
    lv_obj_set_width(punchline_label, 340);
    lv_label_set_long_mode(punchline_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(punchline_label, 0, 95);
    lv_obj_set_style_text_color(punchline_label, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_text_font(punchline_label, &lv_font_montserrat_14, 0);

    // Action buttons
    lv_obj_t *btn_next = lv_btn_create(container);
    lv_obj_set_size(btn_next, 100, 35);
    lv_obj_set_pos(btn_next, 150, 280);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_next, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_border_width(btn_next, 1, 0);

    lv_obj_t *next_label = lv_label_create(btn_next);
    lv_label_set_text(next_label, "Next Joke");
    lv_obj_center(next_label);
    lv_obj_set_style_text_color(next_label, COLOR_ACCENT_GREEN, 0);

    ESP_LOGI(TAG, "Jokes screen created");
    return root;
}

// ==================== RADIO SKILL ====================

lv_obj_t *ui_create_radio_screen(const char *station, const char *now_playing) {
    ESP_LOGI(TAG, "Creating radio screen");

    lv_obj_t *root = lv_scr_act();

    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 350);
    lv_obj_set_pos(container, 33, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header
    lv_obj_t *header = lv_label_create(container);
    lv_label_set_text(header, "📻 Radio");
    lv_obj_set_pos(header, 5, 5);
    lv_obj_set_style_text_color(header, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

    // Station info
    lv_obj_t *station_label = lv_label_create(container);
    lv_label_set_text(station_label, "Streaming: BBC Radio 1");
    lv_obj_set_pos(station_label, 10, 35);
    lv_obj_set_style_text_color(station_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(station_label, &lv_font_montserrat_14, 0);

    // Now playing (scrolling)
    lv_obj_t *playing_label = lv_label_create(container);
    lv_label_set_text(playing_label, now_playing ? now_playing : "♫ The Weeknd - Blinding Lights");
    lv_obj_set_width(playing_label, 360);
    lv_label_set_long_mode(playing_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(playing_label, 10, 60);
    lv_obj_set_style_text_color(playing_label, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_text_font(playing_label, &lv_font_montserrat_14, 0);

    // Playback controls
    lv_obj_t *ctrl_container = lv_obj_create(container);
    lv_obj_set_size(ctrl_container, 360, 50);
    lv_obj_set_pos(ctrl_container, 20, 100);
    lv_obj_set_style_bg_opa(ctrl_container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(ctrl_container, LV_OPA_0, 0);

    // Thumbs up
    lv_obj_t *btn_like = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_like, 50, 50);
    lv_obj_set_pos(btn_like, 20, 0);
    lv_obj_set_style_bg_opa(btn_like, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(btn_like, LV_OPA_0, 0);

    lv_obj_t *like_label = lv_label_create(btn_like);
    lv_label_set_text(like_label, "👍");
    lv_obj_center(like_label);
    lv_obj_set_style_text_font(like_label, &lv_font_montserrat_28, 0);

    // Stop
    lv_obj_t *btn_stop = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_stop, 60, 60);
    lv_obj_set_pos(btn_stop, 150, -5);
    lv_obj_set_style_bg_color(btn_stop, COLOR_ACCENT_RED, 0);
    lv_obj_set_style_bg_opa(btn_stop, LV_OPA_20, 0);
    lv_obj_set_style_border_color(btn_stop, COLOR_ACCENT_RED, 0);
    lv_obj_set_style_border_width(btn_stop, 1, 0);
    lv_obj_set_style_radius(btn_stop, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *stop_label = lv_label_create(btn_stop);
    lv_label_set_text(stop_label, "⏹");
    lv_obj_center(stop_label);
    lv_obj_set_style_text_color(stop_label, COLOR_ACCENT_RED, 0);

    // Skip
    lv_obj_t *btn_skip = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_skip, 50, 50);
    lv_obj_set_pos(btn_skip, 290, 0);
    lv_obj_set_style_bg_opa(btn_skip, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(btn_skip, LV_OPA_0, 0);

    lv_obj_t *skip_label = lv_label_create(btn_skip);
    lv_label_set_text(skip_label, "→");
    lv_obj_center(skip_label);
    lv_obj_set_style_text_color(skip_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(skip_label, &lv_font_montserrat_24, 0);

    // Station list (with current highlighted)
    lv_obj_t *stations_label = lv_label_create(container);
    lv_label_set_text(stations_label, "Stations:");
    lv_obj_set_pos(stations_label, 10, 170);
    lv_obj_set_style_text_color(stations_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(stations_label, &lv_font_montserrat_12, 0);

    const char *station_list[] = {"• BBC Radio 1", "○ BBC Radio 2", "○ Classic FM"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *station_item = lv_label_create(container);
        lv_label_set_text(station_item, station_list[i]);
        lv_obj_set_pos(station_item, 20, 190 + (i * 25));
        lv_obj_set_style_text_color(station_item, i == 0 ? COLOR_ACCENT_GREEN : COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(station_item, &lv_font_montserrat_12, 0);
    }

    ESP_LOGI(TAG, "Radio screen created");
    return root;
}

// ==================== CRYPTO SKILL ====================

lv_obj_t *ui_create_crypto_screen(const char *price_btc, const char *price_eth) {
    ESP_LOGI(TAG, "Creating crypto screen");

    lv_obj_t *root = lv_scr_act();

    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 350);
    lv_obj_set_pos(container, 33, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header
    lv_obj_t *header = lv_label_create(container);
    lv_label_set_text(header, "💹 Crypto Prices");
    lv_obj_set_pos(header, 5, 5);
    lv_obj_set_style_text_color(header, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

    // Bitcoin
    int y_pos = 40;

    lv_obj_t *btc_card = lv_obj_create(container);
    lv_obj_set_size(btc_card, 380, 60);
    lv_obj_set_pos(btc_card, 10, y_pos);
    lv_obj_set_style_bg_color(btc_card, COLOR_ACCENT_GRAY, 0);
    lv_obj_set_style_bg_opa(btc_card, LV_OPA_10, 0);
    lv_obj_set_style_border_opa(btc_card, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(btc_card, 8, 0);

    lv_obj_t *btc_symbol = lv_label_create(btc_card);
    lv_label_set_text(btc_symbol, "Bitcoin");
    lv_obj_set_pos(btc_symbol, 5, 8);
    lv_obj_set_style_text_color(btc_symbol, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(btc_symbol, &lv_font_montserrat_14, 0);

    lv_obj_t *btc_price = lv_label_create(btc_card);
    lv_label_set_text(btc_price, price_btc ? price_btc : "$62,450");
    lv_obj_set_pos(btc_price, 300, 8);
    lv_obj_set_style_text_color(btc_price, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_text_font(btc_price, &lv_font_montserrat_14, 0);

    lv_obj_t *btc_change = lv_label_create(btc_card);
    lv_label_set_text(btc_change, "↑ 2.3% today");
    lv_obj_set_pos(btc_change, 5, 28);
    lv_obj_set_style_text_color(btc_change, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_text_font(btc_change, &lv_font_montserrat_12, 0);

    // Ethereum
    y_pos += 80;

    lv_obj_t *eth_card = lv_obj_create(container);
    lv_obj_set_size(eth_card, 380, 60);
    lv_obj_set_pos(eth_card, 10, y_pos);
    lv_obj_set_style_bg_color(eth_card, COLOR_ACCENT_GRAY, 0);
    lv_obj_set_style_bg_opa(eth_card, LV_OPA_10, 0);
    lv_obj_set_style_border_opa(eth_card, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(eth_card, 8, 0);

    lv_obj_t *eth_symbol = lv_label_create(eth_card);
    lv_label_set_text(eth_symbol, "Ethereum");
    lv_obj_set_pos(eth_symbol, 5, 8);
    lv_obj_set_style_text_color(eth_symbol, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(eth_symbol, &lv_font_montserrat_14, 0);

    lv_obj_t *eth_price = lv_label_create(eth_card);
    lv_label_set_text(eth_price, price_eth ? price_eth : "$2,580");
    lv_obj_set_pos(eth_price, 310, 8);
    lv_obj_set_style_text_color(eth_price, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_text_font(eth_price, &lv_font_montserrat_14, 0);

    lv_obj_t *eth_change = lv_label_create(eth_card);
    lv_label_set_text(eth_change, "↓ 0.8% today");
    lv_obj_set_pos(eth_change, 5, 28);
    lv_obj_set_style_text_color(eth_change, COLOR_ACCENT_RED, 0);
    lv_obj_set_style_text_font(eth_change, &lv_font_montserrat_12, 0);

    // Action buttons
    lv_obj_t *btn_refresh = lv_btn_create(container);
    lv_obj_set_size(btn_refresh, 100, 35);
    lv_obj_set_pos(btn_refresh, 50, 280);
    lv_obj_set_style_bg_opa(btn_refresh, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_refresh, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_border_width(btn_refresh, 1, 0);

    lv_obj_t *refresh_label = lv_label_create(btn_refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);
    lv_obj_set_style_text_color(refresh_label, COLOR_ACCENT_GREEN, 0);

    // Last update
    lv_obj_t *update_label = lv_label_create(container);
    lv_label_set_text(update_label, "Last update: 3:40 PM");
    lv_obj_set_pos(update_label, 10, 330);
    lv_obj_set_style_text_color(update_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(update_label, &lv_font_montserrat_10, 0);

    ESP_LOGI(TAG, "Crypto screen created");
    return root;
}

// ==================== BOOK READER SKILL ====================

lv_obj_t *ui_create_book_reader_screen(const char *title, const char *chapter, int page, int total_pages) {
    ESP_LOGI(TAG, "Creating book reader screen");

    lv_obj_t *root = lv_scr_act();

    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 350);
    lv_obj_set_pos(container, 33, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header: Book title
    lv_obj_t *title_label = lv_label_create(container);
    lv_label_set_text(title_label, title ? title : "The Hobbit");
    lv_obj_set_pos(title_label, 10, 5);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);

    // Chapter info
    lv_obj_t *chapter_label = lv_label_create(container);
    lv_label_set_text(chapter_label, chapter ? chapter : "Chapter 3: A Short Rest");
    lv_obj_set_width(chapter_label, 360);
    lv_label_set_long_mode(chapter_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(chapter_label, 10, 25);
    lv_obj_set_style_text_color(chapter_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(chapter_label, &lv_font_montserrat_12, 0);

    // Progress bar
    lv_obj_t *progress_bar = lv_bar_create(container);
    lv_obj_set_size(progress_bar, 360, 8);
    lv_obj_set_pos(progress_bar, 10, 45);
    lv_bar_set_range(progress_bar, 0, total_pages);
    lv_bar_set_value(progress_bar, page, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, COLOR_ACCENT_GRAY, 0);

    // Page counter
    char page_str[32];
    snprintf(page_str, sizeof(page_str), "Page %d/%d", page, total_pages);
    lv_obj_t *page_label = lv_label_create(container);
    lv_label_set_text(page_label, page_str);
    lv_obj_set_pos(page_label, 10, 58);
    lv_obj_set_style_text_color(page_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(page_label, &lv_font_montserrat_12, 0);

    // Book content (scrollable)
    lv_obj_t *content = lv_label_create(container);
    lv_label_set_text(content, "In a hole in the ground\nthere lived a hobbit.\n\nNot a nasty, dirty, wet hole,\nfilled with the ends of worms\nand an oozy smell...");
    lv_obj_set_width(content, 360);
    lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(content, 10, 80);
    lv_obj_set_style_text_color(content, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(content, &lv_font_montserrat_14, 0);

    // Playback controls
    lv_obj_t *ctrl_container = lv_obj_create(container);
    lv_obj_set_size(ctrl_container, 360, 50);
    lv_obj_set_pos(ctrl_container, 20, 250);
    lv_obj_set_style_bg_opa(ctrl_container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(ctrl_container, LV_OPA_0, 0);

    // Previous chapter
    lv_obj_t *btn_prev = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_prev, 60, 40);
    lv_obj_set_pos(btn_prev, 5, 5);
    lv_obj_set_style_bg_opa(btn_prev, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(btn_prev, LV_OPA_0, 0);

    lv_obj_t *prev_label = lv_label_create(btn_prev);
    lv_label_set_text(prev_label, "|◄");
    lv_obj_center(prev_label);
    lv_obj_set_style_text_color(prev_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_16, 0);

    // Pause/Play
    lv_obj_t *btn_play = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_play, 50, 50);
    lv_obj_set_pos(btn_play, 155, 0);
    lv_obj_set_style_bg_color(btn_play, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_opa(btn_play, LV_OPA_20, 0);
    lv_obj_set_style_border_color(btn_play, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_border_width(btn_play, 1, 0);
    lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *play_label = lv_label_create(btn_play);
    lv_label_set_text(play_label, "⏸");
    lv_obj_center(play_label);
    lv_obj_set_style_text_color(play_label, COLOR_ACCENT_BLUE, 0);

    // Next chapter
    lv_obj_t *btn_next = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_next, 60, 40);
    lv_obj_set_pos(btn_next, 295, 5);
    lv_obj_set_style_bg_opa(btn_next, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(btn_next, LV_OPA_0, 0);

    lv_obj_t *next_label = lv_label_create(btn_next);
    lv_label_set_text(next_label, "►|");
    lv_obj_center(next_label);
    lv_obj_set_style_text_color(next_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_16, 0);

    // Speed indicator
    lv_obj_t *speed_label = lv_label_create(container);
    lv_label_set_text(speed_label, "Speed: 1x");
    lv_obj_set_pos(speed_label, 10, 315);
    lv_obj_set_style_text_color(speed_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(speed_label, &lv_font_montserrat_10, 0);

    ESP_LOGI(TAG, "Book reader screen created");
    return root;
}

// ==================== STORY GENERATOR SKILL ====================

lv_obj_t *ui_create_story_generator_screen(const char *story_text) {
    ESP_LOGI(TAG, "Creating story generator screen");

    lv_obj_t *root = lv_scr_act();

    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_size(container, 400, 350);
    lv_obj_set_pos(container, 33, 50);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(container, LV_OPA_0, 0);

    // Header
    lv_obj_t *header = lv_label_create(container);
    lv_label_set_text(header, "📖 Story Time");
    lv_obj_set_pos(header, 5, 5);
    lv_obj_set_style_text_color(header, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

    // Story content (scrollable, auto-scrolls with TTS)
    lv_obj_t *story_text_obj = lv_label_create(container);
    lv_label_set_text(story_text_obj, story_text ? story_text : "Once upon a time, in a land far away...");
    lv_obj_set_width(story_text_obj, 370);
    lv_label_set_long_mode(story_text_obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(story_text_obj, 15, 40);
    lv_obj_set_style_text_color(story_text_obj, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(story_text_obj, &lv_font_montserrat_14, 0);

    // Playback controls
    lv_obj_t *ctrl_container = lv_obj_create(container);
    lv_obj_set_size(ctrl_container, 360, 50);
    lv_obj_set_pos(ctrl_container, 20, 200);
    lv_obj_set_style_bg_opa(ctrl_container, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(ctrl_container, LV_OPA_0, 0);

    // Pause
    lv_obj_t *btn_pause = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_pause, 50, 50);
    lv_obj_set_pos(btn_pause, 65, 0);
    lv_obj_set_style_bg_color(btn_pause, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_opa(btn_pause, LV_OPA_20, 0);
    lv_obj_set_style_border_color(btn_pause, COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_border_width(btn_pause, 1, 0);
    lv_obj_set_style_radius(btn_pause, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *pause_label = lv_label_create(btn_pause);
    lv_label_set_text(pause_label, "⏸");
    lv_obj_center(pause_label);
    lv_obj_set_style_text_color(pause_label, COLOR_ACCENT_BLUE, 0);

    // Continue
    lv_obj_t *btn_continue = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_continue, 100, 35);
    lv_obj_set_pos(btn_continue, 140, 7);
    lv_obj_set_style_bg_opa(btn_continue, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_continue, COLOR_ACCENT_GREEN, 0);
    lv_obj_set_style_border_width(btn_continue, 1, 0);

    lv_obj_t *continue_label = lv_label_create(btn_continue);
    lv_label_set_text(continue_label, "Continue");
    lv_obj_center(continue_label);
    lv_obj_set_style_text_color(continue_label, COLOR_ACCENT_GREEN, 0);

    // New story
    lv_obj_t *btn_new = lv_btn_create(ctrl_container);
    lv_obj_set_size(btn_new, 80, 35);
    lv_obj_set_pos(btn_new, 260, 7);
    lv_obj_set_style_bg_opa(btn_new, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_new, COLOR_ACCENT_GRAY, 0);
    lv_obj_set_style_border_width(btn_new, 1, 0);

    lv_obj_t *new_label = lv_label_create(btn_new);
    lv_label_set_text(new_label, "New");
    lv_obj_center(new_label);
    lv_obj_set_style_text_color(new_label, COLOR_TEXT_SECONDARY, 0);

    // Footer: Attribution
    lv_obj_t *footer = lv_label_create(container);
    lv_label_set_text(footer, "Generated by Claude");
    lv_obj_set_pos(footer, 10, 315);
    lv_obj_set_style_text_color(footer, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_10, 0);

    ESP_LOGI(TAG, "Story generator screen created");
    return root;
}
