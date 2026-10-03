#include "ui_appearance.h"

namespace {
bool lightAppearance = false;
}

extern "C" void espplants_ui_set_light(bool light) {
    lightAppearance = light;
}

extern "C" bool espplants_ui_is_light(void) {
    return lightAppearance;
}

extern "C" lv_color_t espplants_ui_color(uint32_t canonical_dark_color) {
    return lv_color_hex(espplants_ui_resolve_canonical_hex(canonical_dark_color,
                                                           lightAppearance));
}

extern "C" lv_color_t espplants_ui_source_color(uint32_t source_color,
                                                  bool source_is_light) {
    return lv_color_hex(espplants_ui_resolve_source_hex(source_color,
                                                        source_is_light,
                                                        lightAppearance));
}
