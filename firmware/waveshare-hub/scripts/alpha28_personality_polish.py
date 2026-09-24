Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.27 zero-allocation personality selector" not in s:
    raise RuntimeError("alpha.28 requires current alpha.27 source")

if "Alpha.28 personality selection polish" in s:
    Return()

def replace_once(old,new,what):
    global s
    if old not in s:
        raise RuntimeError("alpha.28 anchor missing: "+what)
    s=s.replace(old,new,1)

# Marker.
s=s.replace(
    "// Alpha.27 zero-allocation personality selector\n",
    "// Alpha.27 zero-allocation personality selector\n"
    "// Alpha.28 personality selection polish\n",
    1)

# Keep the pending selection visibly highlighted in the existing btnmatrix.
anchor='''bool personalityDialogActive = false;

void themeEvent(lv_event_t *event) {
'''
replacement='''bool personalityDialogActive = false;

void refreshPersonalitySelection() {
  if (!renameKeyboard) return;
  lv_btnmatrix_set_one_checked(renameKeyboard,true);
  for (uint16_t i=0;i<6;++i) {
    lv_btnmatrix_set_btn_ctrl(renameKeyboard,i,LV_BTNMATRIX_CTRL_CHECKABLE);
    if (i==static_cast<uint16_t>(pendingTheme))
      lv_btnmatrix_set_btn_ctrl(renameKeyboard,i,LV_BTNMATRIX_CTRL_CHECKED);
    else
      lv_btnmatrix_clear_btn_ctrl(renameKeyboard,i,LV_BTNMATRIX_CTRL_CHECKED);
  }
}

void themeEvent(lv_event_t *event) {
'''
replace_once(anchor,replacement,"personality highlight helper")

# Lower the personality header/hint and initialize checked state every time it opens.
old='''  pendingTheme=phraseTheme;
  personalityDialogActive=true;
  label(renameTitle,"PLANT PERSONALITY");

  char hint[96]{};
  snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
           espplants_phrases::themeName(pendingTheme));
  label(renameHint,hint);

  // Reuse the existing rename btnmatrix. No new LVGL objects are allocated.
  lv_obj_add_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(renameKeyboard,18,82);
  lv_obj_set_size(renameKeyboard,764,380);
  lv_btnmatrix_set_map(renameKeyboard,kPersonalityMap);
'''
new='''  pendingTheme=phraseTheme;
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
replace_once(old,new,"personality open styling")

# Restore normal rename header geometry/btnmatrix state when personality closes.
old='''  if (personalityDialogActive) {
    personalityDialogActive=false;
    lv_obj_clear_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(renameKeyboard,18,158);
    lv_obj_set_size(renameKeyboard,764,304);
  }
'''
new='''  if (personalityDialogActive) {
    personalityDialogActive=false;
    lv_btnmatrix_set_one_checked(renameKeyboard,false);
    lv_btnmatrix_clear_btn_ctrl_all(
        renameKeyboard,
        static_cast<lv_btnmatrix_ctrl_t>(
            LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_CHECKED));
    lv_obj_clear_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_pos(renameKeyboard,18,158);
    lv_obj_set_size(renameKeyboard,764,304);
  }
'''
replace_once(old,new,"personality close restore")

# Re-apply the highlight immediately after any theme row is tapped.
old='''    char hint[96]{};
    snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
             espplants_phrases::themeName(pendingTheme));
    label(renameHint,hint);
    return;
'''
new='''    refreshPersonalitySelection();
    char hint[96]{};
    snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
             espplants_phrases::themeName(pendingTheme));
    label(renameHint,hint);
    return;
'''
replace_once(old,new,"selection highlight update")

# Main Settings row: put each caption directly above its own control.
old='''  cap = lv_label_create(setup);
  lv_label_set_text(cap, "PERSONALITY");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xB7C8BC), 0);
  lv_obj_set_pos(cap, 22, 67);

  lv_obj_t *unitButton = lv_btn_create(setup);
  lv_obj_set_size(unitButton, 92, 46);
  lv_obj_set_pos(unitButton, 242, 56);
'''
new='''  cap = lv_label_create(setup);
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
replace_once(old,new,"settings captions and temp control")

old='''  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 120, 46);
  lv_obj_set_pos(themeButton, 112, 56);
'''
new='''  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 120, 40);
  lv_obj_set_pos(themeButton, 112, 66);
'''
replace_once(old,new,"settings personality control")

p.write_text(s,encoding="utf-8")
