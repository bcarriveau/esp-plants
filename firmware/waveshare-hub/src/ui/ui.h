#ifndef ESP_PLANTS_EEZ_UI_H
#define ESP_PLANTS_EEZ_UI_H

#include <lvgl.h>
#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);
void ui_tick(void);
void loadScreen(enum ScreensEnum screenId);

#ifdef __cplusplus
}
#endif

#endif
