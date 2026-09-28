#include "ui.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static int16_t currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index < 0) {
        return 0;
    }
    return ((lv_obj_t **)&objects)[index];
}

void loadScreen(enum ScreensEnum screenId) {
    currentScreen = (int16_t)screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(currentScreen);
    if (screen) {
        lv_scr_load(screen);
    }
}

#if defined(EEZ_LVGL_SIMULATOR)

typedef enum {
    SIM_PAGE_HOME = 0,
    SIM_PAGE_ALL,
    SIM_PAGE_PLANT,
    SIM_PAGE_SETTINGS,
    SIM_PAGE_ADVANCED
} SimPage;

static void simSetHidden(lv_obj_t *obj, bool hidden) {
    if (!obj) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void simShowPage(SimPage page) {
    simSetHidden(objects.home_page, page != SIM_PAGE_HOME);
    simSetHidden(objects.all_page, page != SIM_PAGE_ALL);
    simSetHidden(objects.plant_page, page != SIM_PAGE_PLANT);
    simSetHidden(objects.settings_page, page != SIM_PAGE_SETTINGS);
    simSetHidden(objects.advanced_page, page != SIM_PAGE_ADVANCED);

    const lv_color_t active = lv_color_hex(0x1E3529);
    const lv_color_t idle = lv_color_hex(0x151F1A);
    lv_obj_set_style_bg_color(objects.nav_home, page == SIM_PAGE_HOME ? active : idle,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(objects.nav_all, page == SIM_PAGE_ALL ? active : idle,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(objects.nav_plant, page == SIM_PAGE_PLANT ? active : idle,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(objects.nav_settings,
                              page == SIM_PAGE_SETTINGS ? active : idle,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void simPageEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const SimPage page = (SimPage)(uintptr_t)lv_event_get_user_data(event);
    simShowPage(page);
}

static void simOpenModal(lv_obj_t *modal) {
    if (!modal) return;
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(modal);
}

static void simCloseModalEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(event);
    simSetHidden(modal, true);
}

static void simOpenUpdateEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simSetHidden(objects.release_notes_modal, true);
    simSetHidden(objects.wifi_forget_confirm, true);
    simOpenModal(objects.update_modal);
}

static void simCloseUpdateEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simSetHidden(objects.release_notes_modal, true);
    simSetHidden(objects.wifi_forget_confirm, true);
    simSetHidden(objects.update_modal, true);
}

static void simOpenReleaseNotesEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (objects.release_notes_label) {
        lv_label_set_text(objects.release_notes_label,
                          "FULL SIM PREVIEW\n\nRelease notes are populated by the real update service on the Waveshare.");
    }
    simOpenModal(objects.update_modal);
    simOpenModal(objects.release_notes_modal);
}

static void simOpenForgetConfirmEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simOpenModal(objects.update_modal);
    simOpenModal(objects.wifi_forget_confirm);
}

static void simOpenRenameEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const char *title = (const char *)lv_event_get_user_data(event);
    if (title && objects.rename_title) lv_label_set_text(objects.rename_title, title);
    if (objects.rename_hint) lv_label_set_text(objects.rename_hint, "Full Sim preview - firmware saves the real value.");
    if (objects.rename_input) {
        lv_textarea_set_text(objects.rename_input, "");
        lv_textarea_set_cursor_pos(objects.rename_input, LV_TEXTAREA_CURSOR_LAST);
    }
    simOpenModal(objects.rename_modal);
}

static void simRenameKeyboardEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    const uint16_t id = lv_btnmatrix_get_selected_btn(objects.rename_keyboard);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char *key = lv_btnmatrix_get_btn_text(objects.rename_keyboard, id);
    if (!key) return;

    if (strcmp(key, "CANCEL") == 0 || strcmp(key, "SAVE") == 0) {
        simSetHidden(objects.rename_modal, true);
    } else if (strcmp(key, "DEL") == 0) {
        lv_textarea_del_char(objects.rename_input);
    } else if (strcmp(key, "SPACE") == 0) {
        lv_textarea_add_text(objects.rename_input, " ");
    } else if (strcmp(key, "abc") != 0) {
        lv_textarea_add_text(objects.rename_input, key);
    }
}

static void simOpenPairEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const char *title = (const char *)lv_event_get_user_data(event);
    if (!title) title = "PAIRING PREVIEW";
    if (objects.pair_title) lv_label_set_text(objects.pair_title, title);
    if (objects.pair_instruction)
        lv_label_set_text(objects.pair_instruction,
                          "Full Sim preview of this dialog. Zigbee behavior runs on the Waveshare.");
    if (objects.pair_status) lv_label_set_text(objects.pair_status, "SIMULATOR PREVIEW");
    if (objects.pair_primary_label) lv_label_set_text(objects.pair_primary_label, "DONE");
    if (objects.pair_secondary_label) lv_label_set_text(objects.pair_secondary_label, "CLOSE");
    simSetHidden(objects.pair_primary, false);
    simOpenModal(objects.pair_modal);
}

static void simFeaturedPlantEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simShowPage(SIM_PAGE_PLANT);
}

static void simInitNavigation(void) {
    lv_obj_add_event_cb(objects.nav_home, simPageEvent, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SIM_PAGE_HOME);
    lv_obj_add_event_cb(objects.nav_all, simPageEvent, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SIM_PAGE_ALL);
    lv_obj_add_event_cb(objects.nav_plant, simPageEvent, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SIM_PAGE_PLANT);
    lv_obj_add_event_cb(objects.nav_settings, simPageEvent, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SIM_PAGE_SETTINGS);

    lv_obj_add_event_cb(objects.home_featured_card, simFeaturedPlantEvent,
                        LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(objects.settings_advanced_button, simPageEvent,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)SIM_PAGE_ADVANCED);
    lv_obj_add_event_cb(objects.advanced_back_button, simPageEvent,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)SIM_PAGE_SETTINGS);

    lv_obj_add_event_cb(objects.settings_network_button, simOpenUpdateEvent,
                        LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(objects.header_update_button, simOpenUpdateEvent,
                        LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(objects.update_close_button, simCloseUpdateEvent,
                        LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(objects.update_check_button, simOpenReleaseNotesEvent,
                        LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(objects.release_notes_back_button, simCloseModalEvent,
                        LV_EVENT_CLICKED, objects.release_notes_modal);
    lv_obj_add_event_cb(objects.update_forget_button, simOpenForgetConfirmEvent,
                        LV_EVENT_CLICKED, 0);
    lv_obj_add_event_cb(objects.wifi_forget_cancel_button, simCloseModalEvent,
                        LV_EVENT_CLICKED, objects.wifi_forget_confirm);
    lv_obj_add_event_cb(objects.wifi_forget_confirm_button, simCloseModalEvent,
                        LV_EVENT_CLICKED, objects.wifi_forget_confirm);

    lv_obj_add_event_cb(objects.settings_device_button, simOpenRenameEvent,
                        LV_EVENT_CLICKED, (void *)"RENAME ESP PLANTS");
    lv_obj_add_event_cb(objects.rename_button, simOpenRenameEvent,
                        LV_EVENT_CLICKED, (void *)"RENAME PLANT");
    lv_obj_add_event_cb(objects.advanced_rename_button, simOpenRenameEvent,
                        LV_EVENT_CLICKED, (void *)"RENAME REPEATER");
    lv_obj_add_event_cb(objects.rename_keyboard, simRenameKeyboardEvent,
                        LV_EVENT_VALUE_CHANGED, 0);

    lv_obj_add_event_cb(objects.settings_pair_button, simOpenPairEvent,
                        LV_EVENT_CLICKED, (void *)"ADD SENSOR");
    lv_obj_add_event_cb(objects.advanced_add_button, simOpenPairEvent,
                        LV_EVENT_CLICKED, (void *)"ADD REPEATER");
    lv_obj_add_event_cb(objects.replace_button, simOpenPairEvent,
                        LV_EVENT_CLICKED, (void *)"REPLACE SENSOR");
    lv_obj_add_event_cb(objects.remove_button, simOpenPairEvent,
                        LV_EVENT_CLICKED, (void *)"REMOVE SENSOR?");
    lv_obj_add_event_cb(objects.advanced_remove_button, simOpenPairEvent,
                        LV_EVENT_CLICKED, (void *)"REMOVE REPEATER?");
    lv_obj_add_event_cb(objects.pair_primary, simCloseModalEvent,
                        LV_EVENT_CLICKED, objects.pair_modal);
    lv_obj_add_event_cb(objects.pair_secondary, simCloseModalEvent,
                        LV_EVENT_CLICKED, objects.pair_modal);

    simShowPage(SIM_PAGE_HOME);
}

#endif

void ui_init(void) {
    create_screens();
#if defined(EEZ_LVGL_SIMULATOR)
    simInitNavigation();
#endif
    loadScreen(SCREEN_ID_HOME);
}

void ui_tick(void) {
    if (currentScreen >= 0) {
        tick_screen(currentScreen);
    }
}