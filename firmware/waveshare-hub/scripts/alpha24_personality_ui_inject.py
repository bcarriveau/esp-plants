Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.23 persistent phrase theme selector" not in s:
    raise RuntimeError("alpha.24 requires current alpha.23 source")

if "Alpha.24 personality picker modal" in s:
    Return()

def replace_once(old,new,what):
    global s
    if old not in s:
        raise RuntimeError("alpha.24 anchor missing: "+what)
    s=s.replace(old,new,1)

replace_once(
'''lv_obj_t *settingsTheme = nullptr;
lv_obj_t *settingsPair = nullptr;
''',
'''lv_obj_t *settingsTheme = nullptr;
// Alpha.24 personality picker modal
lv_obj_t *themeModal = nullptr;
lv_obj_t *themeChoiceButtons[6]{};
espplants_phrases::Theme pendingTheme = espplants_phrases::Theme::MIXED;
lv_obj_t *settingsPair = nullptr;
''',
"theme modal globals")

start=s.find("void themeEvent(lv_event_t *event) {")
if start<0:
    raise RuntimeError("alpha.24 anchor missing: themeEvent")
end=s.find("\n}\n",start)
if end<0:
    raise RuntimeError("alpha.24 anchor missing: themeEvent end")
end+=3
replacement=r'''void refreshThemeDialogSelection() {
  for (uint8_t i=0;i<6;++i) {
    if (!themeChoiceButtons[i]) continue;
    const bool selected=i==static_cast<uint8_t>(pendingTheme);
    lv_obj_set_style_bg_color(themeChoiceButtons[i],
                              lv_color_hex(selected ? 0x3F7A4E : 0x233029),0);
    lv_obj_set_style_border_width(themeChoiceButtons[i],selected ? 2 : 1,0);
    lv_obj_set_style_border_color(themeChoiceButtons[i],
                                  lv_color_hex(selected ? 0x7BC18B : 0x405348),0);
  }
}

void themeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !themeModal) return;
  pendingTheme=phraseTheme;
  refreshThemeDialogSelection();
  lv_obj_clear_flag(themeModal,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(themeModal);
}

void themeChoiceEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const intptr_t value=reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  if (value<0 || value>static_cast<intptr_t>(espplants_phrases::Theme::MIXED)) return;
  pendingTheme=static_cast<espplants_phrases::Theme>(value);
  refreshThemeDialogSelection();
}

void themeCancelEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !themeModal) return;
  pendingTheme=phraseTheme;
  lv_obj_add_flag(themeModal,LV_OBJ_FLAG_HIDDEN);
}

void themeSaveEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !themeModal) return;
  if (pendingTheme!=phraseTheme) {
    phraseTheme=pendingTheme;
    preferences.putUChar("phrase_theme",static_cast<uint8_t>(phraseTheme));
    for (auto &sensor:sensors) sensor.phraseRotation=espplants_phrases::Rotation{};
    Serial.printf("[settings] phrase theme=%s\n",
                  espplants_phrases::themeName(phraseTheme));
  }
  lv_obj_add_flag(themeModal,LV_OBJ_FLAG_HIDDEN);
  uiDirty=true;
}
'''
s=s[:start]+replacement+s[end:]

replace_once('lv_label_set_text(cap, "PERSONALITY                 TEMP");',
             'lv_label_set_text(cap, "PERSONALITY");',
             "settings row caption")
replace_once('lv_obj_set_style_text_font(settingsUnit, &lv_font_montserrat_24, 0);',
             'lv_obj_set_style_text_font(settingsUnit, &lv_font_montserrat_14, 0);',
             "temperature unit font")

replace_once(
'''  label(settingsUnit, useFahrenheit ? "F" : "C");
  label(settingsTheme, espplants_phrases::themeName(phraseTheme));
''',
'''  label(settingsUnit, useFahrenheit ? "TEMP °F" : "TEMP °C");
  snprintf(text,sizeof(text),"%s  >",espplants_phrases::themeName(phraseTheme));
  label(settingsTheme,text);
''',
"settings labels")

modal=r'''
void buildThemeDialog(lv_obj_t *screen) {
  themeModal=lv_obj_create(screen);
  lv_obj_set_pos(themeModal,0,0);
  lv_obj_set_size(themeModal,800,480);
  lv_obj_set_style_radius(themeModal,0,0);
  lv_obj_set_style_border_width(themeModal,0,0);
  lv_obj_set_style_bg_color(themeModal,lv_color_hex(0x101814),0);
  lv_obj_set_style_pad_all(themeModal,0,0);
  lv_obj_clear_flag(themeModal,LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box=lv_obj_create(themeModal);
  lv_obj_set_pos(box,140,55);
  lv_obj_set_size(box,520,350);
  lv_obj_set_style_radius(box,20,0);
  lv_obj_set_style_border_width(box,1,0);
  lv_obj_set_style_border_color(box,lv_color_hex(0x304138),0);
  lv_obj_set_style_bg_color(box,lv_color_hex(0x18231D),0);
  lv_obj_set_style_pad_all(box,0,0);
  lv_obj_clear_flag(box,LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title=lv_label_create(box);
  lv_label_set_text(title,"PLANT PERSONALITY");
  lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);
  lv_obj_set_style_text_color(title,lv_color_hex(0xE5ECE7),0);
  lv_obj_set_pos(title,24,16);

  lv_obj_t *hint=lv_label_create(box);
  lv_label_set_text(hint,"Choose a phrase style. SAVE applies it.");
  lv_obj_set_style_text_font(hint,&lv_font_montserrat_12,0);
  lv_obj_set_style_text_color(hint,lv_color_hex(0x8DA695),0);
  lv_obj_set_pos(hint,24,47);

  static const char *const names[6]={
    "CLASSIC","FUNNY","SARCASTIC","DRAMATIC","RUDE","MIXED - ALL"
  };
  for (uint8_t i=0;i<6;++i) {
    lv_obj_t *button=lv_btn_create(box);
    themeChoiceButtons[i]=button;
    lv_obj_set_size(button,472,32);
    lv_obj_set_pos(button,24,70+i*38);
    lv_obj_set_style_radius(button,9,0);
    lv_obj_add_event_cb(button,themeChoiceEvent,LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_t *name=lv_label_create(button);
    lv_label_set_text(name,names[i]);
    lv_obj_set_style_text_font(name,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(name,lv_color_hex(0xE5ECE7),0);
    lv_obj_center(name);
  }

  lv_obj_t *cancel=lv_btn_create(box);
  lv_obj_set_size(cancel,224,40);
  lv_obj_set_pos(cancel,24,300);
  lv_obj_set_style_radius(cancel,10,0);
  lv_obj_set_style_bg_color(cancel,lv_color_hex(0x233029),0);
  lv_obj_set_style_border_width(cancel,1,0);
  lv_obj_set_style_border_color(cancel,lv_color_hex(0x405348),0);
  lv_obj_add_event_cb(cancel,themeCancelEvent,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *cancelLabel=lv_label_create(cancel);
  lv_label_set_text(cancelLabel,"CANCEL");
  lv_obj_set_style_text_font(cancelLabel,&lv_font_montserrat_14,0);
  lv_obj_center(cancelLabel);

  lv_obj_t *save=lv_btn_create(box);
  lv_obj_set_size(save,224,40);
  lv_obj_set_pos(save,272,300);
  lv_obj_set_style_radius(save,10,0);
  lv_obj_set_style_bg_color(save,lv_color_hex(0x3F7A4E),0);
  lv_obj_add_event_cb(save,themeSaveEvent,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *saveLabel=lv_label_create(save);
  lv_label_set_text(saveLabel,"SAVE");
  lv_obj_set_style_text_font(saveLabel,&lv_font_montserrat_14,0);
  lv_obj_center(saveLabel);

  pendingTheme=phraseTheme;
  refreshThemeDialogSelection();
  lv_obj_add_flag(themeModal,LV_OBJ_FLAG_HIDDEN);
}
'''

anchor="\nvoid buildPairDialog(lv_obj_t *screen) {"
if anchor not in s:
    raise RuntimeError("alpha.24 anchor missing: buildPairDialog")
s=s.replace(anchor,"\n"+modal+anchor,1)

replace_once(
'''  buildNav(screen);
  buildRename(screen);
''',
'''  buildNav(screen);
  buildThemeDialog(screen);
  buildRename(screen);
''',
"buildUi theme dialog")

p.write_text(s,encoding="utf-8")
