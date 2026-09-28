#ifndef ESP_PLANTS_EEZ_SCREENS_H
#define ESP_PLANTS_EEZ_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_HOME = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *home;
    lv_obj_t *home_page;
    lv_obj_t *home_featured_card;
    lv_obj_t *home_section;
    lv_obj_t *home_summary;
    lv_obj_t *home_name;
    lv_obj_t *home_mood;
    lv_obj_t *home_soil_caption;
    lv_obj_t *home_soil;
    lv_obj_t *home_temp_caption;
    lv_obj_t *home_temp;
    lv_obj_t *home_humidity_caption;
    lv_obj_t *home_humidity;
    lv_obj_t *home_bar;
    lv_obj_t *home_warning;
    lv_obj_t *sensor_details_hint;
    lv_obj_t *your_plants_card;
    lv_obj_t *your_plants_title;
    lv_obj_t *home_list;
} objects_t;

extern objects_t objects;

void create_screen_home();
void tick_screen_home();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif