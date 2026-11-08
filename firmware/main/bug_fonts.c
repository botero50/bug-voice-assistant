#include "bug_fonts.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "BUG_FONTS";

extern const uint8_t montserrat_start[] asm("_binary_Montserrat_Medium_ttf_start");
extern const uint8_t montserrat_end[] asm("_binary_Montserrat_Medium_ttf_end");

const lv_font_t *bug_font_small = &lv_font_montserrat_16;
const lv_font_t *bug_font_body = &lv_font_montserrat_20;
const lv_font_t *bug_font_time = &lv_font_montserrat_28;
const lv_font_t *bug_font_huge = &lv_font_montserrat_28;

// LVGL's TinyTTF turns every letter into a picture each time it is drawn, which is slow: a list of words took about a tenth of
// a second to draw and scrolling stuttered. These fonts keep the pictures and the sizes of the letters already drawn, so each
// letter is made once. The cache lives in the large (external) memory.
#define DSC_SLOTS 512
#define BMP_SLOTS 512

typedef struct {
    uint32_t letter, next;
    bool used, found;
    lv_font_glyph_dsc_t dsc;
} dsc_slot_t;

typedef struct {
    uint32_t letter;
    bool used;
    uint8_t *bitmap;
} bmp_slot_t;

typedef struct {
    lv_font_t font;  // what LVGL draws with (first, so a pointer to it is a pointer to this)
    lv_font_t *ttf;  // the real font
    dsc_slot_t *dsc_slots;
    bmp_slot_t *bmp_slots;
} cached_font_t;

static bool cached_get_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t letter, uint32_t next) {
    cached_font_t *c = (cached_font_t *)font;
    uint32_t h = (letter * 2654435761u ^ next * 40503u) % DSC_SLOTS;
    for (int probe = 0; probe < 8; probe++) {
        dsc_slot_t *s = &c->dsc_slots[(h + probe) % DSC_SLOTS];
        if (s->used && s->letter == letter && s->next == next) {
            if (!s->found) return false;
            *out = s->dsc;
            out->resolved_font = font;
            return true;
        }
        if (!s->used) {  // an empty slot: ask the real font and remember the answer
            lv_font_glyph_dsc_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            bool found = c->ttf->get_glyph_dsc(c->ttf, &tmp, letter, next);
            s->used = true;
            s->letter = letter;
            s->next = next;
            s->found = found;
            s->dsc = tmp;
            if (!found) return false;
            *out = tmp;
            out->resolved_font = font;
            return true;
        }
    }
    // the neighbourhood is full: no caching for this letter
    bool found = c->ttf->get_glyph_dsc(c->ttf, out, letter, next);
    if (found) out->resolved_font = font;
    return found;
}

static const uint8_t *cached_get_bitmap(const lv_font_t *font, uint32_t letter) {
    cached_font_t *c = (cached_font_t *)font;
    uint32_t h = (letter * 2654435761u) % BMP_SLOTS;
    for (int probe = 0; probe < 8; probe++) {
        bmp_slot_t *s = &c->bmp_slots[(h + probe) % BMP_SLOTS];
        if (s->used && s->letter == letter) return s->bitmap;
        if (!s->used) {
            lv_font_glyph_dsc_t dsc;
            memset(&dsc, 0, sizeof(dsc));
            if (!c->ttf->get_glyph_dsc(c->ttf, &dsc, letter, 0)) return NULL;
            const uint8_t *made = c->ttf->get_glyph_bitmap(c->ttf, letter);
            if (!made) return NULL;
            size_t bytes = (size_t)((dsc.box_w * (dsc.bpp ? dsc.bpp : 8) + 7) / 8) * dsc.box_h;
            uint8_t *copy = bytes ? heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM) : NULL;
            if (!copy) return made;  // no room to keep it: use it as it is (it is only valid until the next one is made)
            memcpy(copy, made, bytes);
            s->used = true;
            s->letter = letter;
            s->bitmap = copy;
            return copy;
        }
    }
    return c->ttf->get_glyph_bitmap(c->ttf, letter);
}

static const lv_font_t *make_font(lv_coord_t size, const lv_font_t *builtin) {
    lv_font_t *ttf = lv_tiny_ttf_create_data(montserrat_start, montserrat_end - montserrat_start, size);
    if (!ttf) {
        ESP_LOGW(TAG, "Could not create the %d px font, using the built-in one", (int)size);
        return builtin;
    }
    cached_font_t *c = heap_caps_calloc(1, sizeof(cached_font_t), MALLOC_CAP_SPIRAM);
    if (c) {
        c->dsc_slots = heap_caps_calloc(DSC_SLOTS, sizeof(dsc_slot_t), MALLOC_CAP_SPIRAM);
        c->bmp_slots = heap_caps_calloc(BMP_SLOTS, sizeof(bmp_slot_t), MALLOC_CAP_SPIRAM);
    }
    if (!c || !c->dsc_slots || !c->bmp_slots) {
        ESP_LOGW(TAG, "No memory for the letter cache of the %d px font", (int)size);
        ttf->fallback = builtin;
        return ttf;
    }
    c->ttf = ttf;
    c->font = *ttf;  // sizes, baseline, underline: the same as the real font
    c->font.get_glyph_dsc = cached_get_dsc;
    c->font.get_glyph_bitmap = cached_get_bitmap;
    c->font.fallback = builtin;  // LVGL's symbols (wifi, battery) live in the built-in font
    return &c->font;
}

esp_err_t bug_fonts_init(void) {
    bug_font_small = make_font(17, &lv_font_montserrat_16);
    bug_font_body = make_font(23, &lv_font_montserrat_20);
    bug_font_time = make_font(34, &lv_font_montserrat_28);
    bug_font_huge = make_font(64, &lv_font_montserrat_28);
    ESP_LOGI(TAG, "Fonts ready");
    return ESP_OK;
}
