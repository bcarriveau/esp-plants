Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.28 personality selection polish" not in s:
    raise RuntimeError("alpha.29 requires current alpha.28 source")

if "Alpha.29 personality layout cleanup" in s:
    Return()

def replace_once(old,new,what):
    global s
    if old not in s:
        raise RuntimeError("alpha.29 anchor missing: "+what)
    s=s.replace(old,new,1)

s=s.replace(
    "// Alpha.28 personality selection polish\n",
    "// Alpha.28 personality selection polish\n// Alpha.29 personality layout cleanup\n",
    1
)

old = '''  pendingTheme=phraseTheme;
  personalityDialogActive=true;
  label(renameTitle,"PLANT PERSONALITY");
  lv_obj_set_pos(renameTitle,18,14);
  lv_obj_set_pos(renameHint,20,44);

  char hint[96]{};
  snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
           espplants_phrases::themeName(pendingTheme));
  label(renameHint,hint);

  // Reuse the existing rename btnmatrix. No new LVGL objects are allocated.
  lv_obj_add_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(renameKeyboard,18,82);
  lv_obj_set_size(renameKeyboard,764,380);
  lv_btnmatrix_set_map(renameKeyboard,kPersonalityMap);
  lv_obj_set_style_bg_color(renameKeyboard,lv_color_hex(0x3F7A4E),
                            LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(renameKeyboard,2,
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(renameKeyboard,lv_color_hex(0x8DD39C),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  refreshPersonalitySelection();
'''
new = '''  pendingTheme=phraseTheme;
  personalityDialogActive=true;
  label(renameTitle,"PLANT PERSONALITY");
  lv_obj_set_pos(renameTitle,36,18);
  lv_obj_set_pos(renameHint,36,50);
  lv_obj_set_width(renameHint,620);

  char hint[96]{};
  snprintf(hint,sizeof(hint),"Selected: %s. SAVE applies it.",
           espplants_phrases::themeName(pendingTheme));
  label(renameHint,hint);

  // Reuse the existing rename btnmatrix. No new LVGL objects are allocated.
  lv_obj_add_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(renameKeyboard,36,94);
  lv_obj_set_size(renameKeyboard,728,336);
  lv_btnmatrix_set_map(renameKeyboard,kPersonalityMap);
  lv_obj_set_style_bg_color(renameKeyboard,lv_color_hex(0x3F7A4E),
                            LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(renameKeyboard,2,
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(renameKeyboard,lv_color_hex(0x8DD39C),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
  refreshPersonalitySelection();
'''
replace_once(old,new,"themeEvent layout")

old = '''  if (personalityDialogActive) {
    personalityDialogActive=false;
    lv_btnmatrix_set_one_checked(renameKeyboard,false);
    lv_btnmatrix_clear_btn_ctrl_all(
        renameKeyboard,
        static_cast<lv_btnmatrix_ctrl_t>(
            LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_CHECKED));
    lv_obj_clear_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_width(renameHint,520);
    lv_obj_set_pos(renameKeyboard,18,158);
    lv_obj_set_size(renameKeyboard,764,304);
  }
'''
new = '''  if (personalityDialogActive) {
    personalityDialogActive=false;
    lv_btnmatrix_set_one_checked(renameKeyboard,false);
    lv_btnmatrix_clear_btn_ctrl_all(
        renameKeyboard,
        static_cast<lv_btnmatrix_ctrl_t>(
            LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_CHECKED));
    lv_obj_clear_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_width(renameHint,520);
    lv_obj_set_pos(renameKeyboard,18,158);
    lv_obj_set_size(renameKeyboard,764,304);
  }
'''
replace_once(old,new,"closeRename restore width")

old = '''    refreshPersonalitySelection();
    char hint[96]{};
    snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
             espplants_phrases::themeName(pendingTheme));
    label(renameHint,hint);
    return;
'''
new = '''    refreshPersonalitySelection();
    char hint[96]{};
    snprintf(hint,sizeof(hint),"Selected: %s. SAVE applies it.",
             espplants_phrases::themeName(pendingTheme));
    label(renameHint,hint);
    return;
'''
replace_once(old,new,"selection hint text")

old = '''  cap = lv_label_create(setup);
  lv_label_set_text(cap, "PERSONALITY");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 112, 50);

  lv_obj_t *tempCap = lv_label_create(setup);
  lv_label_set_text(tempCap, "TEMP UNIT");
  lv_obj_set_style_text_font(tempCap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tempCap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(tempCap, 242, 50);

  lv_obj_t *unitButton = lv_btn_create(setup);
  lv_obj_set_size(unitButton, 92, 40);
  lv_obj_set_pos(unitButton, 242, 66);
'''
new = '''  cap = lv_label_create(setup);
  lv_label_set_text(cap, "PERSONALITY");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 127, 44);

  lv_obj_t *tempCap = lv_label_create(setup);
  lv_label_set_text(tempCap, "TEMP UNIT");
  lv_obj_set_style_text_font(tempCap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tempCap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(tempCap, 247, 44);

  lv_obj_t *unitButton = lv_btn_create(setup);
  lv_obj_set_size(unitButton, 92, 40);
  lv_obj_set_pos(unitButton, 242, 62);
'''
replace_once(old,new,"settings captions block")

old = '''  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 120, 40);
  lv_obj_set_pos(themeButton, 112, 66);
'''
new = '''  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 120, 40);
  lv_obj_set_pos(themeButton, 112, 62);
'''
replace_once(old,new,"theme button y")

old = '''  label(settingsUnit, useFahrenheit ? "TEMP °F" : "TEMP °C");
  label(settingsTheme, espplants_phrases::themeName(phraseTheme));
'''
new = '''  label(settingsUnit, useFahrenheit ? "°F" : "°C");
  label(settingsTheme, espplants_phrases::themeName(phraseTheme));
'''
replace_once(old,new,"settings button text")

p.write_text(s,encoding="utf-8")
