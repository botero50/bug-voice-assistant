#include "bug_fonts.h"
#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "BUG_FONTS";

extern const uint8_t montserrat_start[] asm("_binary_Montserrat_Medium_ttf_start");
extern const uint8_t montserrat_end[] asm("_binary_Montserrat_Medium_ttf_end");

const lv_font_t *bug_font_small = &lv_font_montserrat_16;
const lv_font_t *bug_font_body = &lv_font_montserrat_20;
const lv_font_t *bug_font_time = &lv_font_montserrat_28;
const lv_font_t *bug_font_huge = &lv_font_montserrat_28;

static const lv_font_t *make_font(lv_coord_t size, const lv_font_t *builtin) {
    lv_font_t *font = lv_tiny_ttf_create_data(montserrat_start, montserrat_end - montserrat_start, size);
    if (!font) {
        ESP_LOGW(TAG, "Could not create the %d px font, using the built-in one", (int)size);
        return builtin;
    }
    font->fallback = builtin;  // LVGL's symbols (wifi, battery) live in the built-in font
    return font;
}

esp_err_t bug_fonts_init(void) {
    bug_font_small = make_font(17, &lv_font_montserrat_16);
    bug_font_body = make_font(23, &lv_font_montserrat_20);
    bug_font_time = make_font(34, &lv_font_montserrat_28);
    bug_font_huge = make_font(64, &lv_font_montserrat_28);
    ESP_LOGI(TAG, "Fonts ready");
    return ESP_OK;
}
