#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

// Near-identical EEZ source colors used only as semantic tokens. The resolver
// always emits the exact public palette colors on-screen.
#define ESP_PLANTS_UI_CARD_DARK_TOKEN         0x18231CU
#define ESP_PLANTS_UI_CARD_LIGHT_TOKEN        0xF3F8F3U
#define ESP_PLANTS_UI_LIGHT_ALT_WHITE_TOKEN   0xFFFFFEU
#define ESP_PLANTS_UI_LIGHT_LITERAL_WHITE_TOKEN 0xFFFFFCU
#define ESP_PLANTS_UI_LIGHT_ALT_GREEN_TOKEN   0xCFE7D4U
#define ESP_PLANTS_UI_LIGHT_ALT_RED_TOKEN     0x7A3029U

static inline uint32_t espplants_ui_dark_to_light_hex(uint32_t color) {
    if (color == ESP_PLANTS_UI_CARD_DARK_TOKEN ||
        color == ESP_PLANTS_UI_CARD_LIGHT_TOKEN) return 0xF3F8F4U;
    switch (color) {
        case 0x101814U: return 0xFFFFFFU;
        case 0x18231DU: return 0xFAFCFBU;
        case 0x0C2518U: return 0xF3F8F4U;
        case 0x111A16U: return 0xF6F9F7U;
        case 0x151F1AU: return 0xF9FBFAU;
        case 0x1E3529U: return 0xE1F0E5U;
        case 0x1D2922U: return 0xF7FAF8U;
        case 0x233029U: return 0xEEF3F0U;
        case 0x244F39U: return 0xE1EFE5U;
        case 0x3F7A4EU: return 0xCFE7D5U;
        case 0x12583AU: return 0xDAF0DFU;
        case 0x131C17U: return 0xFFFFFFU;
        case 0x304138U: return 0xD7E0DAU;
        case 0x405348U: return 0xC4D0C8U;
        case 0x2A352EU: return 0xE9EEEBU;
        case 0x3A536FU: return 0xE6EFF7U;
        case 0x7A4037U: return 0xF3DCD7U;
        case 0x8E493EU: return 0xF3DAD5U;
        case 0xFFF4EEU: return 0x7A3028U;
        case 0xF7EDE9U: return 0x7A3028U;
        case 0xE5ECE7U: return 0x16221BU;
        case 0xB7C8BCU: return 0x4A5C51U;
        case 0xC1D0C6U: return 0x3D5044U;
        case 0x8DA695U: return 0x617268U;
        case 0xAABBAFU: return 0x526459U;
        case 0xCBE6D2U: return 0x2F6F45U;
        case 0xD1DED5U: return 0x405248U;
        case 0x9DB5A5U: return 0x596D60U;
        case 0xA5C3ADU: return 0x4F6D59U;
        case 0x85D892U: return 0x2F7B48U;
        case 0xB4FFCDU: return 0x1F6B39U;
        case 0x93A69AU: return 0x64766BU;
        case 0xF2C66DU: return 0x986300U;
        case 0xE2B276U: return 0x875900U;
        case 0x3FFF9BU: return 0x5FA979U;
        case 0x8DD39CU: return 0x7EB68CU;
        case 0x3A2723U: return 0xFBEAE6U;
        case 0x3A3023U: return 0xFAF3E5U;
        case 0x4A2924U: return 0xF5DDD8U;
        case 0x3E7A50U: return 0xCFE7D5U;
        case 0x26342DU: return 0xEDF2EFU;
        default: return color;
    }
}

static inline uint32_t espplants_ui_light_to_dark_hex(uint32_t color) {
    if (color == ESP_PLANTS_UI_CARD_DARK_TOKEN ||
        color == ESP_PLANTS_UI_CARD_LIGHT_TOKEN) return 0x18231DU;
    switch (color) {
        case ESP_PLANTS_UI_LIGHT_ALT_WHITE_TOKEN: return 0x131C17U;
        case ESP_PLANTS_UI_LIGHT_LITERAL_WHITE_TOKEN: return 0xFFFFFFU;
        case ESP_PLANTS_UI_LIGHT_ALT_GREEN_TOKEN: return 0x3E7A50U;
        case ESP_PLANTS_UI_LIGHT_ALT_RED_TOKEN: return 0xF7EDE9U;
        case 0xFFFFFFU: return 0x101814U;
        case 0xFAFCFBU: return 0x18231DU;
        case 0xF3F8F4U: return 0x0C2518U;
        case 0xF6F9F7U: return 0x111A16U;
        case 0xF9FBFAU: return 0x151F1AU;
        case 0xE1F0E5U: return 0x1E3529U;
        case 0xF7FAF8U: return 0x1D2922U;
        case 0xEEF3F0U: return 0x233029U;
        case 0xE1EFE5U: return 0x244F39U;
        case 0xCFE7D5U: return 0x3F7A4EU;
        case 0xDAF0DFU: return 0x12583AU;
        case 0xD7E0DAU: return 0x304138U;
        case 0xC4D0C8U: return 0x405348U;
        case 0xE9EEEBU: return 0x2A352EU;
        case 0xE6EFF7U: return 0x3A536FU;
        case 0xF3DCD7U: return 0x7A4037U;
        case 0xF3DAD5U: return 0x8E493EU;
        case 0x7A3028U: return 0xFFF4EEU;
        case 0x16221BU: return 0xE5ECE7U;
        case 0x4A5C51U: return 0xB7C8BCU;
        case 0x3D5044U: return 0xC1D0C6U;
        case 0x617268U: return 0x8DA695U;
        case 0x526459U: return 0xAABBAFU;
        case 0x2F6F45U: return 0xCBE6D2U;
        case 0x405248U: return 0xD1DED5U;
        case 0x596D60U: return 0x9DB5A5U;
        case 0x4F6D59U: return 0xA5C3ADU;
        case 0x2F7B48U: return 0x85D892U;
        case 0x1F6B39U: return 0xB4FFCDU;
        case 0x64766BU: return 0x93A69AU;
        case 0x986300U: return 0xF2C66DU;
        case 0x875900U: return 0xE2B276U;
        case 0x5FA979U: return 0x3FFF9BU;
        case 0x7EB68CU: return 0x8DD39CU;
        case 0xFBEAE6U: return 0x3A2723U;
        case 0xFAF3E5U: return 0x3A3023U;
        case 0xF5DDD8U: return 0x4A2924U;
        case 0xEDF2EFU: return 0x26342DU;
        default: return color;
    }
}

static inline uint32_t espplants_ui_resolve_canonical_hex(uint32_t dark_color,
                                                           bool render_light) {
    if (dark_color == ESP_PLANTS_UI_CARD_DARK_TOKEN ||
        dark_color == ESP_PLANTS_UI_CARD_LIGHT_TOKEN) {
        return render_light ? 0xF3F8F4U : 0x18231DU;
    }
    return render_light ? espplants_ui_dark_to_light_hex(dark_color) : dark_color;
}

static inline uint32_t espplants_ui_resolve_source_hex(uint32_t source_color,
                                                        bool source_is_light,
                                                        bool render_light) {
    if (source_color == ESP_PLANTS_UI_CARD_DARK_TOKEN ||
        source_color == ESP_PLANTS_UI_CARD_LIGHT_TOKEN) {
        return render_light ? 0xF3F8F4U : 0x18231DU;
    }
    if (source_is_light) {
        if (!render_light) return espplants_ui_light_to_dark_hex(source_color);
        if (source_color == ESP_PLANTS_UI_LIGHT_ALT_WHITE_TOKEN ||
            source_color == ESP_PLANTS_UI_LIGHT_LITERAL_WHITE_TOKEN) return 0xFFFFFFU;
        if (source_color == ESP_PLANTS_UI_LIGHT_ALT_GREEN_TOKEN) return 0xCFE7D5U;
        if (source_color == ESP_PLANTS_UI_LIGHT_ALT_RED_TOKEN) return 0x7A3028U;
        return source_color;
    }
    return render_light ? espplants_ui_dark_to_light_hex(source_color) : source_color;
}

#ifdef __cplusplus
extern "C" {
#endif

void espplants_ui_set_light(bool light);
bool espplants_ui_is_light(void);
lv_color_t espplants_ui_color(uint32_t canonical_dark_color);
lv_color_t espplants_ui_source_color(uint32_t source_color, bool source_is_light);

#ifdef __cplusplus
}
#endif
