#include <Arduino.h>
#include <lvgl.h>

namespace {

lv_style_t noTransitionDefault;
lv_style_t noTransitionPressed;
lv_style_t noTransitionChecked;
lv_style_t noTransitionDisabled;
bool stylesInitialized = false;
bool announced = false;

void initNoTransitionStyles() {
  if (stylesInitialized) return;
  stylesInitialized = true;

  lv_style_init(&noTransitionDefault);
  lv_style_set_transition(&noTransitionDefault, nullptr);

  lv_style_init(&noTransitionPressed);
  lv_style_set_transition(&noTransitionPressed, nullptr);

  lv_style_init(&noTransitionChecked);
  lv_style_set_transition(&noTransitionChecked, nullptr);

  lv_style_init(&noTransitionDisabled);
  lv_style_set_transition(&noTransitionDisabled, nullptr);
}

void disableButtonTransitions(lv_obj_t *button) {
  if (!button) return;
  initNoTransitionStyles();

  // LVGL's default theme adds delayed/normal transitions to buttons for both
  // the default and pressed states.  These local styles are added after the
  // theme styles and therefore override only LV_STYLE_TRANSITION.  Geometry,
  // colors, borders, fonts and the existing pressed-state appearance remain
  // owned by the existing UI/theme.
  lv_obj_add_style(button, &noTransitionDefault,
                   LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_style(button, &noTransitionPressed,
                   LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_style(button, &noTransitionChecked,
                   LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_style(button, &noTransitionDisabled,
                   LV_PART_MAIN | LV_STATE_DISABLED);

  if (!announced) {
    announced = true;
    Serial.println("[lvgl] alpha.12 guard: animated button style transitions disabled");
  }
}

}  // namespace

extern "C" lv_obj_t *__real_lv_btn_create(lv_obj_t *parent);

extern "C" lv_obj_t *__wrap_lv_btn_create(lv_obj_t *parent) {
  lv_obj_t *button = __real_lv_btn_create(parent);
  disableButtonTransitions(button);
  return button;
}
