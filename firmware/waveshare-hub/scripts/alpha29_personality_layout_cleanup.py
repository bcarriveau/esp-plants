Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.28 personality selection polish" not in s:
    raise RuntimeError("alpha.29 requires current alpha.28 source")

def replace_if_present(old,new):
    global s
    if old in s:
        s=s.replace(old,new,1)
        return True
    return False

def require_any(what,*needles):
    if not any(n in s for n in needles):
        raise RuntimeError("alpha.29 unexpected source near: "+what)

if "Alpha.29 personality layout cleanup" not in s:
    s=s.replace(
        "// Alpha.28 personality selection polish\n",
        "// Alpha.28 personality selection polish\n"
        "// Alpha.29 personality layout cleanup\n",
        1)

old_open='''  pendingTheme=phraseTheme;
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
new_open='''  pendingTheme=phraseTheme;
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
replace_if_present(old_open,new_open)
require_any("personality open layout",
            "lv_obj_set_pos(renameKeyboard,36,94);",
            "lv_obj_set_pos(renameKeyboard,18,82);")

restore_anchor='''    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_pos(renameKeyboard,18,158);
'''
restore_new='''    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_width(renameHint,520);
    lv_obj_set_pos(renameKeyboard,18,158);
'''
if restore_anchor in s:
    s=s.replace(restore_anchor,restore_new,1)
require_any("closeRename restore",
            "lv_obj_set_pos(renameHint,20,36);\n    lv_obj_set_width(renameHint,520);")

old_hint='''    snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
             espplants_phrases::themeName(pendingTheme));
'''
new_hint='''    snprintf(hint,sizeof(hint),"Selected: %s. SAVE applies it.",
             espplants_phrases::themeName(pendingTheme));
'''
replace_if_present(old_hint,new_hint)
require_any("selection hint",
            'snprintf(hint,sizeof(hint),"Selected: %s. SAVE applies it.",')

old_settings='''  cap = lv_label_create(setup);
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
new_settings='''  cap = lv_label_create(setup);
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
replace_if_present(old_settings,new_settings)
require_any("settings caption geometry",
            "lv_obj_set_pos(cap, 127, 44);")

old_theme='''  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 120, 40);
  lv_obj_set_pos(themeButton, 112, 66);
'''
new_theme='''  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 120, 40);
  lv_obj_set_pos(themeButton, 112, 62);
'''
replace_if_present(old_theme,new_theme)
require_any("theme button geometry",
            "lv_obj_set_pos(themeButton, 112, 62);")

old_refresh='''  label(settingsUnit, useFahrenheit ? "TEMP °F" : "TEMP °C");
  snprintf(text,sizeof(text),"%s  >",espplants_phrases::themeName(phraseTheme));
  label(settingsTheme,text);
'''
new_refresh='''  label(settingsUnit, useFahrenheit ? "°F" : "°C");
  label(settingsTheme, espplants_phrases::themeName(phraseTheme));
'''
replace_if_present(old_refresh,new_refresh)
require_any("settings refresh labels",
            'label(settingsUnit, useFahrenheit ? "°F" : "°C");')

p.write_text(s,encoding="utf-8")
