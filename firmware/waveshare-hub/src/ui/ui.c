#include "ui.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
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

#define SIM_SENSOR_CAPACITY 10
#define SIM_HOME_ROW_HEIGHT 56
#define SIM_HOME_ROW_GAP 7
#define SIM_HOME_ROW_STRIDE (SIM_HOME_ROW_HEIGHT + SIM_HOME_ROW_GAP)
#define SIM_ALL_ROW_HEIGHT 56
#define SIM_ALL_ROW_GAP 6
#define SIM_ALL_ROW_STRIDE (SIM_ALL_ROW_HEIGHT + SIM_ALL_ROW_GAP)

typedef enum {
    SIM_PAGE_HOME = 0,
    SIM_PAGE_ALL,
    SIM_PAGE_PLANT,
    SIM_PAGE_SETTINGS,
    SIM_PAGE_ADVANCED
} SimPage;

typedef struct {
    bool enabled;
    bool reporting;
    bool waterWarning;
    uint8_t moisture;
    int16_t tempF;
    uint8_t humidity;
    uint8_t battery;
    uint8_t lqi;
    char name[16];
} SimSensor;

typedef struct {
    lv_obj_t *box;
    lv_obj_t *name;
    lv_obj_t *moisture;
    lv_obj_t *bar;
} SimHomeRow;

typedef struct {
    lv_obj_t *box;
    lv_obj_t *name;
    lv_obj_t *moisture;
    lv_obj_t *battery;
    lv_obj_t *updated;
} SimAllRow;

typedef enum {
    SIM_ADJUST_MOISTURE = 0,
    SIM_ADJUST_TEMP,
    SIM_ADJUST_HUMIDITY,
    SIM_ADJUST_BATTERY,
    SIM_ADJUST_LQI
} SimAdjustField;

static SimSensor simSensors[SIM_SENSOR_CAPACITY];
static SimHomeRow simHomeRows[SIM_SENSOR_CAPACITY];
static SimAllRow simAllRows[SIM_SENSOR_CAPACITY];
static uint8_t simSelectedSensor = 0;
static SimPage simCurrentPage = SIM_PAGE_HOME;

static lv_obj_t *simButton = 0;
static lv_obj_t *simControls = 0;
static lv_obj_t *simControlSensorLabel = 0;
static lv_obj_t *simEnableLabel = 0;
static lv_obj_t *simReportLabel = 0;
static lv_obj_t *simWaterLabel = 0;
static lv_obj_t *simMoistureValue = 0;
static lv_obj_t *simTempValue = 0;
static lv_obj_t *simHumidityValue = 0;
static lv_obj_t *simBatteryValue = 0;
static lv_obj_t *simLqiValue = 0;

static void simUpdateViews(void);

static void simSetHidden(lv_obj_t *obj, bool hidden) {
    if (!obj) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void simSetText(lv_obj_t *obj, const char *text) {
    if (!obj || !text) return;
    lv_label_set_text(obj, text);
}

static size_t simEnabledCount(void) {
    size_t count = 0;
    for (size_t i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        if (simSensors[i].enabled) ++count;
    }
    return count;
}

static size_t simReportingCount(void) {
    size_t count = 0;
    for (size_t i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        if (simSensors[i].enabled && simSensors[i].reporting) ++count;
    }
    return count;
}

static int simFirstEnabled(void) {
    for (int i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        if (simSensors[i].enabled) return i;
    }
    return -1;
}

static int simDetailSensor(void) {
    if (simSelectedSensor < SIM_SENSOR_CAPACITY && simSensors[simSelectedSensor].enabled) {
        return (int)simSelectedSensor;
    }
    return simFirstEnabled();
}

static int simFeaturedSensor(void) {
    int best = -1;
    uint8_t bestMoisture = 255;
    for (int i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        const SimSensor *sensor = &simSensors[i];
        if (!sensor->enabled || !sensor->reporting) continue;
        if (sensor->waterWarning) return i;
        if (best < 0 || sensor->moisture < bestMoisture) {
            best = i;
            bestMoisture = sensor->moisture;
        }
    }
    return best;
}

static const char *simMood(const SimSensor *sensor) {
    if (!sensor || !sensor->reporting) return "Waiting for this plant to check in";
    if (sensor->waterWarning || sensor->moisture <= 15) return "Water me soon";
    if (sensor->moisture <= 30) return "Getting thirsty";
    if (sensor->moisture <= 70) return "Doing fine";
    if (sensor->moisture <= 85) return "Plenty of water";
    return "Very wet";
}

static void simShowPage(SimPage page) {
    simCurrentPage = page;
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
    if (simButton) lv_obj_move_foreground(simButton);
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
    if (simButton) lv_obj_move_foreground(simButton);
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
    const int featured = simFeaturedSensor();
    if (featured >= 0) simSelectedSensor = (uint8_t)featured;
    simUpdateViews();
    simShowPage(SIM_PAGE_PLANT);
}

static lv_obj_t *simMakeButton(lv_obj_t *parent, int x, int y, int w, int h,
                               const char *text, lv_event_cb_t cb, void *userData,
                               lv_obj_t **labelOut) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x405348), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x233029), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    if (cb) lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, userData);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE5ECE7), 0);
    lv_obj_center(label);
    if (labelOut) *labelOut = label;
    return button;
}

static lv_obj_t *simMakeCaption(lv_obj_t *parent, int x, int y, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xB7C8BC), 0);
    return label;
}

static void simSensorRowEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const uintptr_t encoded = (uintptr_t)lv_event_get_user_data(event);
    if (encoded == 0 || encoded > SIM_SENSOR_CAPACITY) return;
    simSelectedSensor = (uint8_t)(encoded - 1U);
    simUpdateViews();
    simShowPage(SIM_PAGE_PLANT);
}

static void simCreateRows(void) {
    lv_obj_add_flag(objects.home_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(objects.home_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(objects.home_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(objects.home_list, 0, 0);

    lv_obj_add_flag(objects.all_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(objects.all_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(objects.all_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(objects.all_list, 0, 0);

    for (int i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        SimHomeRow *home = &simHomeRows[i];
        home->box = lv_obj_create(objects.home_list);
        lv_obj_set_pos(home->box, 0, i * SIM_HOME_ROW_STRIDE);
        lv_obj_set_size(home->box, 228, SIM_HOME_ROW_HEIGHT);
        lv_obj_set_style_radius(home->box, 12, 0);
        lv_obj_set_style_border_width(home->box, 0, 0);
        lv_obj_set_style_bg_color(home->box, lv_color_hex(0x1D2922), 0);
        lv_obj_set_style_pad_all(home->box, 8, 0);
        lv_obj_clear_flag(home->box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(home->box, simSensorRowEvent, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i + 1));

        home->name = lv_label_create(home->box);
        lv_obj_set_width(home->name, 145);
        lv_label_set_long_mode(home->name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(home->name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(home->name, lv_color_hex(0xE5ECE7), 0);

        home->moisture = lv_label_create(home->box);
        lv_obj_set_style_text_font(home->moisture, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(home->moisture, lv_color_hex(0xE5ECE7), 0);
        lv_obj_align(home->moisture, LV_ALIGN_TOP_RIGHT, -2, -2);

        home->bar = lv_bar_create(home->box);
        lv_obj_set_pos(home->bar, 2, 30);
        lv_obj_set_size(home->bar, 208, 9);
        lv_bar_set_range(home->bar, 0, 100);
        lv_obj_set_style_bg_color(home->bar, lv_color_hex(0x2A352E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(home->bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(home->bar, lv_color_hex(0x5E9B68), LV_PART_INDICATOR);

        SimAllRow *all = &simAllRows[i];
        all->box = lv_obj_create(objects.all_list);
        lv_obj_set_pos(all->box, 0, i * SIM_ALL_ROW_STRIDE);
        lv_obj_set_size(all->box, 742, SIM_ALL_ROW_HEIGHT);
        lv_obj_set_style_radius(all->box, 10, 0);
        lv_obj_set_style_border_width(all->box, 0, 0);
        lv_obj_set_style_bg_color(all->box, lv_color_hex(0x1D2922), 0);
        lv_obj_set_style_pad_all(all->box, 8, 0);
        lv_obj_clear_flag(all->box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(all->box, simSensorRowEvent, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i + 1));

        all->name = lv_label_create(all->box);
        lv_obj_set_pos(all->name, 4, 9);
        lv_obj_set_width(all->name, 285);
        lv_label_set_long_mode(all->name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(all->name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(all->name, lv_color_hex(0xE5ECE7), 0);

        all->moisture = lv_label_create(all->box);
        lv_obj_set_pos(all->moisture, 305, 8);
        lv_obj_set_width(all->moisture, 110);
        lv_obj_set_style_text_align(all->moisture, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(all->moisture, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(all->moisture, lv_color_hex(0xE5ECE7), 0);

        all->battery = lv_label_create(all->box);
        lv_obj_set_pos(all->battery, 435, 9);
        lv_obj_set_width(all->battery, 115);
        lv_obj_set_style_text_align(all->battery, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(all->battery, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(all->battery, lv_color_hex(0xE5ECE7), 0);

        all->updated = lv_label_create(all->box);
        lv_obj_set_pos(all->updated, 565, 10);
        lv_obj_set_width(all->updated, 155);
        lv_obj_set_style_text_align(all->updated, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(all->updated, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(all->updated, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(all->updated, lv_color_hex(0xD1DED5), 0);
    }
}

static void simInitSensors(void) {
    static const uint8_t moistureDefaults[SIM_SENSOR_CAPACITY] = {18, 34, 52, 76, 83, 44, 29, 67, 58, 91};
    for (int i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        SimSensor *sensor = &simSensors[i];
        memset(sensor, 0, sizeof(*sensor));
        sensor->enabled = false;
        sensor->reporting = true;
        sensor->waterWarning = false;
        sensor->moisture = moistureDefaults[i];
        sensor->tempF = (int16_t)(70 + (i % 5));
        sensor->humidity = (uint8_t)(44 + (i * 3) % 28);
        sensor->battery = (uint8_t)(95 - i * 4);
        sensor->lqi = (uint8_t)(205 - i * 7);
        snprintf(sensor->name, sizeof(sensor->name), "PLANT %d", i + 1);
    }
    simSelectedSensor = 0;
}

static void simUpdateHome(void) {
    char text[96];
    const size_t enabled = simEnabledCount();
    const size_t reporting = simReportingCount();
    const size_t waiting = enabled >= reporting ? enabled - reporting : 0;
    snprintf(text, sizeof(text), "%u REPORTING | %u WAITING",
             (unsigned)reporting, (unsigned)waiting);
    simSetText(objects.home_summary, text);

    int logical = 0;
    for (int i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        SimHomeRow *row = &simHomeRows[i];
        const SimSensor *sensor = &simSensors[i];
        if (!sensor->enabled) {
            simSetHidden(row->box, true);
            continue;
        }
        simSetHidden(row->box, false);
        lv_obj_set_pos(row->box, 0, logical * SIM_HOME_ROW_STRIDE);
        ++logical;
        simSetText(row->name, sensor->name);
        if (sensor->reporting) {
            snprintf(text, sizeof(text), "%u%%", sensor->moisture);
            simSetText(row->moisture, text);
            lv_bar_set_value(row->bar, sensor->moisture, LV_ANIM_OFF);
        } else {
            simSetText(row->moisture, "--%");
            lv_bar_set_value(row->bar, 0, LV_ANIM_OFF);
        }
        lv_obj_set_style_bg_color(row->box,
                                  i == simDetailSensor() ? lv_color_hex(0x1E3529)
                                                         : lv_color_hex(0x1D2922),
                                  0);
    }

    const int featured = simFeaturedSensor();
    if (enabled == 0) {
        simSetText(objects.home_name, "WAITING FOR SENSOR");
        simSetText(objects.home_mood, "Pair a sensor and I'll keep an eye on it");
        simSetText(objects.home_soil, "--%");
        simSetText(objects.home_temp, "--.- F");
        simSetText(objects.home_humidity, "--%");
        lv_bar_set_value(objects.home_bar, 0, LV_ANIM_OFF);
        simSetHidden(objects.home_warning, true);
    } else if (featured < 0) {
        simSetText(objects.home_name, "WAITING FOR REPORTS");
        snprintf(text, sizeof(text), "%u %s waiting to report", (unsigned)waiting,
                 waiting == 1 ? "sensor" : "sensors");
        simSetText(objects.home_mood, text);
        simSetText(objects.home_soil, "--%");
        simSetText(objects.home_temp, "--.- F");
        simSetText(objects.home_humidity, "--%");
        lv_bar_set_value(objects.home_bar, 0, LV_ANIM_OFF);
        simSetHidden(objects.home_warning, true);
    } else {
        const SimSensor *sensor = &simSensors[featured];
        simSetText(objects.home_name, sensor->name);
        simSetText(objects.home_mood, simMood(sensor));
        snprintf(text, sizeof(text), "%u%%", sensor->moisture);
        simSetText(objects.home_soil, text);
        snprintf(text, sizeof(text), "%d.0 F", sensor->tempF);
        simSetText(objects.home_temp, text);
        snprintf(text, sizeof(text), "%u%%", sensor->humidity);
        simSetText(objects.home_humidity, text);
        lv_bar_set_value(objects.home_bar, sensor->moisture, LV_ANIM_OFF);
        simSetHidden(objects.home_warning, !sensor->waterWarning);
    }
}

static void simUpdateAll(void) {
    char text[64];
    const size_t enabled = simEnabledCount();
    const size_t reporting = simReportingCount();
    const size_t waiting = enabled >= reporting ? enabled - reporting : 0;
    snprintf(text, sizeof(text), "%u REPORTING | %u WAITING",
             (unsigned)reporting, (unsigned)waiting);
    simSetText(objects.all_summary, text);

    int logical = 0;
    for (int i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        SimAllRow *row = &simAllRows[i];
        const SimSensor *sensor = &simSensors[i];
        if (!sensor->enabled) {
            simSetHidden(row->box, true);
            continue;
        }
        simSetHidden(row->box, false);
        lv_obj_set_pos(row->box, 0, logical * SIM_ALL_ROW_STRIDE);
        ++logical;
        simSetText(row->name, sensor->name);
        if (sensor->reporting) {
            snprintf(text, sizeof(text), "%u%%", sensor->moisture);
            simSetText(row->moisture, text);
            snprintf(text, sizeof(text), "%u%%", sensor->battery);
            simSetText(row->battery, text);
            simSetText(row->updated, "NOW");
        } else {
            simSetText(row->moisture, "--%");
            simSetText(row->battery, "--%");
            simSetText(row->updated, "WAITING");
        }
        lv_obj_set_style_bg_color(row->box,
                                  i == simDetailSensor() ? lv_color_hex(0x1E3529)
                                                         : lv_color_hex(0x1D2922),
                                  0);
    }
}

static void simUpdatePlant(void) {
    char text[128];
    const int selected = simDetailSensor();
    if (selected < 0) {
        simSetText(objects.detail_slot, "PLANT --");
        simSetText(objects.detail_name, "NO PLANT SELECTED");
        simSetText(objects.detail_mood, "--");
        simSetText(objects.detail_ieee, "--");
        simSetText(objects.detail_soil, "--%");
        simSetText(objects.detail_temp, "--.- F");
        simSetText(objects.detail_humidity, "--%");
        simSetText(objects.detail_battery, "--%");
        simSetText(objects.detail_signal, "LQI --");
        simSetText(objects.detail_updated, "No sensor data yet");
        lv_bar_set_value(objects.detail_bar, 0, LV_ANIM_OFF);
        simSetHidden(objects.detail_warning, true);
        lv_obj_add_state(objects.rename_button, LV_STATE_DISABLED);
        lv_obj_add_state(objects.replace_button, LV_STATE_DISABLED);
        lv_obj_add_state(objects.remove_button, LV_STATE_DISABLED);
        return;
    }

    const SimSensor *sensor = &simSensors[selected];
    snprintf(text, sizeof(text), "PLANT %d", selected + 1);
    simSetText(objects.detail_slot, text);
    simSetText(objects.detail_name, sensor->name);
    simSetText(objects.detail_mood, simMood(sensor));
    if (sensor->reporting) {
        snprintf(text, sizeof(text), "A4:C1:38:8F:FC:40:63:%02X   short 0x%04X",
                 selected + 1, 0x1200 + selected);
        simSetText(objects.detail_ieee, text);
        snprintf(text, sizeof(text), "%u%%", sensor->moisture);
        simSetText(objects.detail_soil, text);
        snprintf(text, sizeof(text), "%d.0 F", sensor->tempF);
        simSetText(objects.detail_temp, text);
        snprintf(text, sizeof(text), "%u%%", sensor->humidity);
        simSetText(objects.detail_humidity, text);
        snprintf(text, sizeof(text), "%u%%", sensor->battery);
        simSetText(objects.detail_battery, text);
        snprintf(text, sizeof(text), "LQI %u", sensor->lqi);
        simSetText(objects.detail_signal, text);
        simSetText(objects.detail_updated, "Updated now");
        lv_bar_set_value(objects.detail_bar, sensor->moisture, LV_ANIM_OFF);
        simSetHidden(objects.detail_warning, !sensor->waterWarning);
    } else {
        snprintf(text, sizeof(text), "A4:C1:38:8F:FC:40:63:%02X   waiting for check-in",
                 selected + 1);
        simSetText(objects.detail_ieee, text);
        simSetText(objects.detail_soil, "--%");
        simSetText(objects.detail_temp, "--.- F");
        simSetText(objects.detail_humidity, "--%");
        simSetText(objects.detail_battery, "--%");
        simSetText(objects.detail_signal, "LQI --");
        simSetText(objects.detail_updated, "Waiting for this plant to check in");
        lv_bar_set_value(objects.detail_bar, 0, LV_ANIM_OFF);
        simSetHidden(objects.detail_warning, true);
    }
    lv_obj_clear_state(objects.rename_button, LV_STATE_DISABLED);
    lv_obj_clear_state(objects.replace_button, LV_STATE_DISABLED);
    lv_obj_clear_state(objects.remove_button, LV_STATE_DISABLED);
}

static void simUpdateSettings(void) {
    char text[96];
    const size_t enabled = simEnabledCount();
    simSetText(objects.settings_h2, "ONLINE");
    snprintf(text, sizeof(text), "%u", (unsigned)enabled);
    simSetText(objects.settings_plants, text);
    snprintf(text, sizeof(text), "READY CH 15 | P %u | R 0", (unsigned)enabled);
    simSetText(objects.settings_zigbee, text);
    simSetText(objects.settings_pair, "ADD SENSOR");
}

static void simUpdateHeader(void) {
    char text[32];
    const size_t enabled = simEnabledCount();
    snprintf(text, sizeof(text), "%u %s", (unsigned)enabled,
             enabled == 1 ? "PLANT" : "PLANTS");
    simSetText(objects.header_count, text);
    simSetHidden(objects.header_update_button, true);
}

static void simUpdateControlLabels(void) {
    if (!simControls) return;
    char text[64];
    SimSensor *sensor = &simSensors[simSelectedSensor];
    snprintf(text, sizeof(text), "PLANT %u / %u", (unsigned)simSelectedSensor + 1U,
             (unsigned)SIM_SENSOR_CAPACITY);
    simSetText(simControlSensorLabel, text);
    simSetText(simEnableLabel, sensor->enabled ? "ENABLED" : "DISABLED");
    simSetText(simReportLabel,
               !sensor->enabled ? "STATE N/A" : (sensor->reporting ? "REPORTING" : "WAITING"));
    simSetText(simWaterLabel, sensor->waterWarning ? "WATER ON" : "WATER OFF");
    snprintf(text, sizeof(text), "%u%%", sensor->moisture);
    simSetText(simMoistureValue, text);
    snprintf(text, sizeof(text), "%d F", sensor->tempF);
    simSetText(simTempValue, text);
    snprintf(text, sizeof(text), "%u%%", sensor->humidity);
    simSetText(simHumidityValue, text);
    snprintf(text, sizeof(text), "%u%%", sensor->battery);
    simSetText(simBatteryValue, text);
    snprintf(text, sizeof(text), "%u", sensor->lqi);
    simSetText(simLqiValue, text);
}

static void simUpdateViews(void) {
    simUpdateHeader();
    simUpdateHome();
    simUpdateAll();
    simUpdatePlant();
    simUpdateSettings();
    simUpdateControlLabels();
}

static void simOpenControlsEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simUpdateControlLabels();
    simSetHidden(simControls, false);
    lv_obj_move_foreground(simControls);
}

static void simCloseControlsEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simSetHidden(simControls, true);
    if (simButton) lv_obj_move_foreground(simButton);
}

static void simPresetEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const size_t wanted = (size_t)(uintptr_t)lv_event_get_user_data(event);
    for (size_t i = 0; i < SIM_SENSOR_CAPACITY; ++i) {
        simSensors[i].enabled = i < wanted;
        if (i < wanted) simSensors[i].reporting = true;
        simSensors[i].waterWarning = false;
    }
    if (wanted > 0 && simSelectedSensor >= wanted) simSelectedSensor = 0;
    simUpdateViews();
    lv_obj_scroll_to_y(objects.home_list, 0, LV_ANIM_OFF);
    lv_obj_scroll_to_y(objects.all_list, 0, LV_ANIM_OFF);
}

static void simPrevSensorEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simSelectedSensor = simSelectedSensor == 0 ? SIM_SENSOR_CAPACITY - 1
                                               : (uint8_t)(simSelectedSensor - 1);
    simUpdateControlLabels();
}

static void simNextSensorEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    simSelectedSensor = (uint8_t)((simSelectedSensor + 1U) % SIM_SENSOR_CAPACITY);
    simUpdateControlLabels();
}

static void simToggleEnabledEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    SimSensor *sensor = &simSensors[simSelectedSensor];
    sensor->enabled = !sensor->enabled;
    if (sensor->enabled) sensor->reporting = true;
    simUpdateViews();
}

static void simToggleReportingEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    SimSensor *sensor = &simSensors[simSelectedSensor];
    if (!sensor->enabled) {
        sensor->enabled = true;
        sensor->reporting = true;
    } else {
        sensor->reporting = !sensor->reporting;
    }
    simUpdateViews();
}

static void simToggleWaterEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    SimSensor *sensor = &simSensors[simSelectedSensor];
    if (!sensor->enabled) sensor->enabled = true;
    if (!sensor->reporting) sensor->reporting = true;
    sensor->waterWarning = !sensor->waterWarning;
    simUpdateViews();
}

static int simClampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void simAdjustEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const intptr_t encoded = (intptr_t)lv_event_get_user_data(event);
    const SimAdjustField field = (SimAdjustField)((encoded >> 16) & 0xFFFF);
    int delta = (int16_t)(encoded & 0xFFFF);
    SimSensor *sensor = &simSensors[simSelectedSensor];
    switch (field) {
        case SIM_ADJUST_MOISTURE:
            sensor->moisture = (uint8_t)simClampInt((int)sensor->moisture + delta, 0, 100);
            break;
        case SIM_ADJUST_TEMP:
            sensor->tempF = (int16_t)simClampInt((int)sensor->tempF + delta, 32, 120);
            break;
        case SIM_ADJUST_HUMIDITY:
            sensor->humidity = (uint8_t)simClampInt((int)sensor->humidity + delta, 0, 100);
            break;
        case SIM_ADJUST_BATTERY:
            sensor->battery = (uint8_t)simClampInt((int)sensor->battery + delta, 0, 100);
            break;
        case SIM_ADJUST_LQI:
            sensor->lqi = (uint8_t)simClampInt((int)sensor->lqi + delta, 0, 255);
            break;
    }
    simUpdateViews();
}

static void *simAdjustData(SimAdjustField field, int delta) {
    const intptr_t encoded = ((intptr_t)field << 16) | (uint16_t)(int16_t)delta;
    return (void *)encoded;
}

static void simCreateControls(void) {
    simButton = simMakeButton(objects.home, 548, 16, 104, 34, "SIM", simOpenControlsEvent, 0, 0);
    lv_obj_set_style_bg_color(simButton, lv_color_hex(0x3A536F), 0);

    simControls = lv_obj_create(objects.home);
    lv_obj_set_pos(simControls, 0, 0);
    lv_obj_set_size(simControls, 800, 480);
    lv_obj_set_style_radius(simControls, 0, 0);
    lv_obj_set_style_border_width(simControls, 0, 0);
    lv_obj_set_style_bg_color(simControls, lv_color_hex(0x101814), 0);
    lv_obj_set_style_pad_all(simControls, 0, 0);
    lv_obj_clear_flag(simControls, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(simControls);
    lv_label_set_text(title, "FULL SIM SENSOR CONTROLS");
    lv_obj_set_pos(title, 20, 12);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE5ECE7), 0);

    lv_obj_t *hint = lv_label_create(simControls);
    lv_label_set_text(hint, "SIMULATOR ONLY - changes here never touch ESP PLANTS firmware or saved data.");
    lv_obj_set_pos(hint, 20, 43);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9DB5A5), 0);

    simMakeButton(simControls, 684, 10, 96, 40, "CLOSE", simCloseControlsEvent, 0, 0);

    simMakeCaption(simControls, 24, 88, "SENSOR PRESETS");
    simMakeButton(simControls, 160, 76, 72, 40, "0", simPresetEvent, (void *)(uintptr_t)0, 0);
    simMakeButton(simControls, 242, 76, 72, 40, "1", simPresetEvent, (void *)(uintptr_t)1, 0);
    simMakeButton(simControls, 324, 76, 72, 40, "3", simPresetEvent, (void *)(uintptr_t)3, 0);
    simMakeButton(simControls, 406, 76, 72, 40, "10", simPresetEvent, (void *)(uintptr_t)10, 0);

    simMakeCaption(simControls, 24, 145, "EDIT SENSOR");
    simMakeButton(simControls, 160, 132, 72, 42, "PREV", simPrevSensorEvent, 0, 0);
    simControlSensorLabel = lv_label_create(simControls);
    lv_obj_set_pos(simControlSensorLabel, 250, 143);
    lv_obj_set_width(simControlSensorLabel, 220);
    lv_obj_set_style_text_align(simControlSensorLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(simControlSensorLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(simControlSensorLabel, lv_color_hex(0xE5ECE7), 0);
    simMakeButton(simControls, 488, 132, 72, 42, "NEXT", simNextSensorEvent, 0, 0);

    simMakeCaption(simControls, 24, 202, "STATE");
    simMakeButton(simControls, 160, 188, 130, 44, "", simToggleEnabledEvent, 0, &simEnableLabel);
    simMakeButton(simControls, 304, 188, 140, 44, "", simToggleReportingEvent, 0, &simReportLabel);
    simMakeButton(simControls, 458, 188, 130, 44, "", simToggleWaterEvent, 0, &simWaterLabel);

    simMakeCaption(simControls, 24, 267, "MOISTURE");
    simMakeButton(simControls, 112, 252, 48, 42, "-", simAdjustEvent, simAdjustData(SIM_ADJUST_MOISTURE, -5), 0);
    simMoistureValue = lv_label_create(simControls);
    lv_obj_set_pos(simMoistureValue, 170, 264);
    lv_obj_set_width(simMoistureValue, 74);
    lv_obj_set_style_text_align(simMoistureValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(simMoistureValue, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(simMoistureValue, lv_color_hex(0xE5ECE7), 0);
    simMakeButton(simControls, 254, 252, 48, 42, "+", simAdjustEvent, simAdjustData(SIM_ADJUST_MOISTURE, 5), 0);

    simMakeCaption(simControls, 382, 267, "TEMP");
    simMakeButton(simControls, 438, 252, 48, 42, "-", simAdjustEvent, simAdjustData(SIM_ADJUST_TEMP, -1), 0);
    simTempValue = lv_label_create(simControls);
    lv_obj_set_pos(simTempValue, 496, 264);
    lv_obj_set_width(simTempValue, 76);
    lv_obj_set_style_text_align(simTempValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(simTempValue, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(simTempValue, lv_color_hex(0xE5ECE7), 0);
    simMakeButton(simControls, 582, 252, 48, 42, "+", simAdjustEvent, simAdjustData(SIM_ADJUST_TEMP, 1), 0);

    simMakeCaption(simControls, 24, 329, "AIR RH");
    simMakeButton(simControls, 112, 314, 48, 42, "-", simAdjustEvent, simAdjustData(SIM_ADJUST_HUMIDITY, -5), 0);
    simHumidityValue = lv_label_create(simControls);
    lv_obj_set_pos(simHumidityValue, 170, 326);
    lv_obj_set_width(simHumidityValue, 74);
    lv_obj_set_style_text_align(simHumidityValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(simHumidityValue, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(simHumidityValue, lv_color_hex(0xE5ECE7), 0);
    simMakeButton(simControls, 254, 314, 48, 42, "+", simAdjustEvent, simAdjustData(SIM_ADJUST_HUMIDITY, 5), 0);

    simMakeCaption(simControls, 382, 329, "BATTERY");
    simMakeButton(simControls, 462, 314, 48, 42, "-", simAdjustEvent, simAdjustData(SIM_ADJUST_BATTERY, -5), 0);
    simBatteryValue = lv_label_create(simControls);
    lv_obj_set_pos(simBatteryValue, 520, 326);
    lv_obj_set_width(simBatteryValue, 74);
    lv_obj_set_style_text_align(simBatteryValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(simBatteryValue, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(simBatteryValue, lv_color_hex(0xE5ECE7), 0);
    simMakeButton(simControls, 604, 314, 48, 42, "+", simAdjustEvent, simAdjustData(SIM_ADJUST_BATTERY, 5), 0);

    simMakeCaption(simControls, 24, 391, "LQI");
    simMakeButton(simControls, 112, 376, 48, 42, "-", simAdjustEvent, simAdjustData(SIM_ADJUST_LQI, -10), 0);
    simLqiValue = lv_label_create(simControls);
    lv_obj_set_pos(simLqiValue, 170, 388);
    lv_obj_set_width(simLqiValue, 74);
    lv_obj_set_style_text_align(simLqiValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(simLqiValue, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(simLqiValue, lv_color_hex(0xE5ECE7), 0);
    simMakeButton(simControls, 254, 376, 48, 42, "+", simAdjustEvent, simAdjustData(SIM_ADJUST_LQI, 10), 0);

    lv_obj_t *note = lv_label_create(simControls);
    lv_label_set_text(note, "Rows are clickable: tap any simulated plant on HOME or ALL SENSORS to open Plant Detail.");
    lv_obj_set_pos(note, 382, 383);
    lv_obj_set_width(note, 360);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(0x9DB5A5), 0);

    simSetHidden(simControls, true);
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

    simInitSensors();
    simCreateRows();
    simCreateControls();
    simUpdateViews();
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