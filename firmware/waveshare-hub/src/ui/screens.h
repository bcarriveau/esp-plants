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
    lv_obj_t *all_page;
    lv_obj_t *all_card;
    lv_obj_t *all_title;
    lv_obj_t *all_summary;
    lv_obj_t *all_head_plant;
    lv_obj_t *all_head_soil;
    lv_obj_t *all_head_battery;
    lv_obj_t *all_head_updated;
    lv_obj_t *all_list;
    lv_obj_t *plant_page;
    lv_obj_t *plant_card;
    lv_obj_t *detail_slot;
    lv_obj_t *detail_name;
    lv_obj_t *detail_mood;
    lv_obj_t *detail_ieee;
    lv_obj_t *rename_button;
    lv_obj_t *obj0;
    lv_obj_t *replace_button;
    lv_obj_t *obj1;
    lv_obj_t *remove_button;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *detail_soil;
    lv_obj_t *obj4;
    lv_obj_t *detail_temp;
    lv_obj_t *obj5;
    lv_obj_t *detail_humidity;
    lv_obj_t *obj6;
    lv_obj_t *detail_battery;
    lv_obj_t *obj7;
    lv_obj_t *detail_signal;
    lv_obj_t *detail_bar;
    lv_obj_t *detail_updated;
    lv_obj_t *detail_warning;
    lv_obj_t *settings_page;
    lv_obj_t *settings_system_card;
    lv_obj_t *settings_system_title;
    lv_obj_t *settings_device_caption;
    lv_obj_t *settings_device_button;
    lv_obj_t *settings_device_name;
    lv_obj_t *settings_h2_caption;
    lv_obj_t *settings_h2;
    lv_obj_t *settings_registered_caption;
    lv_obj_t *settings_plants;
    lv_obj_t *settings_zigbee_caption;
    lv_obj_t *settings_zigbee;
    lv_obj_t *settings_legend;
    lv_obj_t *settings_network_button;
    lv_obj_t *settings_network_button_label;
    lv_obj_t *settings_setup_card;
    lv_obj_t *settings_setup_title;
    lv_obj_t *settings_personality_caption;
    lv_obj_t *settings_temp_caption;
    lv_obj_t *settings_unit_button;
    lv_obj_t *settings_unit;
    lv_obj_t *settings_theme_button;
    lv_obj_t *settings_theme;
    lv_obj_t *settings_plant_sensors_caption;
    lv_obj_t *settings_sensor_hint;
    lv_obj_t *settings_pair_button;
    lv_obj_t *settings_pair;
    lv_obj_t *settings_zigbee_network_caption;
    lv_obj_t *settings_network_hint;
    lv_obj_t *settings_advanced_button;
    lv_obj_t *settings_advanced_button_label;
    lv_obj_t *advanced_page;
    lv_obj_t *advanced_card;
    lv_obj_t *advanced_title;
    lv_obj_t *advanced_summary;
    lv_obj_t *advanced_back_button;
    lv_obj_t *advanced_back_button_label;
    lv_obj_t *advanced_add_button;
    lv_obj_t *advanced_add_button_label;
    lv_obj_t *advanced_head_repeater;
    lv_obj_t *advanced_head_status;
    lv_obj_t *advanced_head_signal;
    lv_obj_t *advanced_list;
    lv_obj_t *advanced_detail;
    lv_obj_t *advanced_rename_button;
    lv_obj_t *advanced_rename_button_label;
    lv_obj_t *advanced_remove_button;
    lv_obj_t *advanced_remove_button_label;
    lv_obj_t *header;
    lv_obj_t *header_title;
    lv_obj_t *obj8;
    lv_obj_t *header_update_button;
    lv_obj_t *obj9;
    lv_obj_t *header_count;
    lv_obj_t *nav_bar;
    lv_obj_t *nav_home;
    lv_obj_t *obj10;
    lv_obj_t *nav_all;
    lv_obj_t *obj11;
    lv_obj_t *nav_plant;
    lv_obj_t *obj12;
    lv_obj_t *nav_settings;
    lv_obj_t *obj13;
    lv_obj_t *rename_modal;
    lv_obj_t *obj14;
    lv_obj_t *rename_title;
    lv_obj_t *rename_hint;
    lv_obj_t *rename_input;
    lv_obj_t *rename_keyboard;
    lv_obj_t *pair_modal;
    lv_obj_t *obj15;
    lv_obj_t *pair_title;
    lv_obj_t *obj16;
    lv_obj_t *pair_instruction;
    lv_obj_t *pair_status;
    lv_obj_t *pair_primary;
    lv_obj_t *pair_primary_label;
    lv_obj_t *pair_secondary;
    lv_obj_t *pair_secondary_label;
    lv_obj_t *update_modal;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *update_close_button;
    lv_obj_t *obj19;
    lv_obj_t *update_network_card;
    lv_obj_t *obj20;
    lv_obj_t *obj21;
    lv_obj_t *update_wifi_state;
    lv_obj_t *update_wifi_detail;
    lv_obj_t *update_setup_button;
    lv_obj_t *update_setup_label;
    lv_obj_t *update_disconnect_button;
    lv_obj_t *update_disconnect_label;
    lv_obj_t *update_forget_button;
    lv_obj_t *obj22;
    lv_obj_t *update_qr_hint;
    lv_obj_t *update_qr_card;
    lv_obj_t *update_portal_info;
    lv_obj_t *update_software_card;
    lv_obj_t *obj23;
    lv_obj_t *obj24;
    lv_obj_t *update_current_version;
    lv_obj_t *obj25;
    lv_obj_t *update_latest_version;
    lv_obj_t *update_h2_version;
    lv_obj_t *update_status;
    lv_obj_t *update_check_button;
    lv_obj_t *update_check_label;
    lv_obj_t *update_install_button;
    lv_obj_t *update_install_label;
    lv_obj_t *obj26;
    lv_obj_t *release_notes_modal;
    lv_obj_t *obj27;
    lv_obj_t *obj28;
    lv_obj_t *release_notes_back_button;
    lv_obj_t *obj29;
    lv_obj_t *release_notes_scroll;
    lv_obj_t *release_notes_label;
    lv_obj_t *release_notes_check_button;
    lv_obj_t *obj30;
    lv_obj_t *wifi_forget_confirm;
    lv_obj_t *obj31;
    lv_obj_t *obj32;
    lv_obj_t *wifi_forget_cancel_button;
    lv_obj_t *obj33;
    lv_obj_t *wifi_forget_confirm_button;
    lv_obj_t *obj34;
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