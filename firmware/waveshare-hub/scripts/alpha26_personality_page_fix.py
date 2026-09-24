Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.25 lazy personality modal startup guard" not in s:
    raise RuntimeError("alpha.26 requires alpha.25 source")

if "Alpha.26 personality settings page" in s:
    Return()

def replace_once(old,new,what):
    global s
    if old not in s:
        raise RuntimeError("alpha.26 anchor missing: "+what)
    s=s.replace(old,new,1)

# Marker + page enum.
s=s.replace(
    "// Alpha.25 lazy personality modal startup guard\n",
    "// Alpha.25 lazy personality modal startup guard\n"
    "// Alpha.26 personality settings page\n",
    1)

replace_once(
    "enum class Page : uint8_t { Home = 0, All = 1, Plant = 2, Settings = 3, Advanced = 4 };",
    "enum class Page : uint8_t { Home = 0, All = 1, Plant = 2, Settings = 3, Advanced = 4, Personality = 5 };",
    "Page enum")

# Replace the full-screen modal pointer with a normal content-page pointer.
replace_once("lv_obj_t *themeModal = nullptr;\n",
             "lv_obj_t *personalityPage = nullptr;\n",
             "personality page pointer")

# showPage(): manage the personality page like Advanced, while keeping SETTINGS
# highlighted in the bottom navigation.
advanced_show='''  if (advancedPage) (page == Page::Advanced) ? lv_obj_clear_flag(advancedPage, LV_OBJ_FLAG_HIDDEN)
                                             : lv_obj_add_flag(advancedPage, LV_OBJ_FLAG_HIDDEN);
'''
advanced_plus=advanced_show+'''  if (personalityPage) (page == Page::Personality) ? lv_obj_clear_flag(personalityPage, LV_OBJ_FLAG_HIDDEN)
                                                       : lv_obj_add_flag(personalityPage, LV_OBJ_FLAG_HIDDEN);
'''
replace_once(advanced_show,advanced_plus,"showPage personality")

replace_once(
'''  if (navSettings) lv_obj_set_style_bg_color(navSettings, page == Page::Settings ? active : idle, 0);
''',
'''  if (navSettings) lv_obj_set_style_bg_color(
      navSettings,
      (page == Page::Settings || page == Page::Personality) ? active : idle, 0);
''',
"settings nav highlight")

# alpha.25 forward declaration is no longer needed.
s=s.replace("void buildThemeDialog(lv_obj_t *screen);\n\n","",1)

# Opening personality now just switches to a normal page. No object creation
# occurs inside an LVGL click callback.
theme_start=s.find("void themeEvent(lv_event_t *event) {")
if theme_start<0:
    raise RuntimeError("alpha.26 anchor missing: themeEvent")
theme_end=s.find("\n}\n",theme_start)
if theme_end<0:
    raise RuntimeError("alpha.26 anchor missing: themeEvent end")
theme_end+=3
theme_fn='''void themeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  pendingTheme=phraseTheme;
  refreshThemeDialogSelection();
  showPage(Page::Personality);
}
'''
s=s[:theme_start]+theme_fn+s[theme_end:]

# Cancel and Save return to Settings. SAVE remains the only point that persists.
cancel_start=s.find("void themeCancelEvent(lv_event_t *event) {")
if cancel_start<0:
    raise RuntimeError("alpha.26 anchor missing: themeCancelEvent")
cancel_end=s.find("\n}\n",cancel_start)
if cancel_end<0:
    raise RuntimeError("alpha.26 anchor missing: themeCancelEvent end")
cancel_end+=3
cancel_fn='''void themeCancelEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  pendingTheme=phraseTheme;
  refreshThemeDialogSelection();
  showPage(Page::Settings);
}
'''
s=s[:cancel_start]+cancel_fn+s[cancel_end:]

save_start=s.find("void themeSaveEvent(lv_event_t *event) {")
if save_start<0:
    raise RuntimeError("alpha.26 anchor missing: themeSaveEvent")
save_end=s.find("\n}\n",save_start)
if save_end<0:
    raise RuntimeError("alpha.26 anchor missing: themeSaveEvent end")
save_end+=3
save_fn='''void themeSaveEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (pendingTheme!=phraseTheme) {
    phraseTheme=pendingTheme;
    preferences.putUChar("phrase_theme",static_cast<uint8_t>(phraseTheme));
    for (auto &sensor:sensors) sensor.phraseRotation=espplants_phrases::Rotation{};
    Serial.printf("[settings] phrase theme=%s\\n",
                  espplants_phrases::themeName(phraseTheme));
  }
  uiDirty=true;
  showPage(Page::Settings);
}
'''
s=s[:save_start]+save_fn+s[save_end:]

# Replace the crash-prone full-screen modal builder with a normal settings page.
builder_start=s.find("void buildThemeDialog(lv_obj_t *screen) {")
if builder_start<0:
    raise RuntimeError("alpha.26 anchor missing: buildThemeDialog")
builder_end=s.find("\nvoid buildPairDialog(lv_obj_t *screen) {",builder_start)
if builder_end<0:
    raise RuntimeError("alpha.26 anchor missing: buildPairDialog after theme dialog")

page_builder=r'''void buildPersonality(lv_obj_t *screen) {
  personalityPage=lv_obj_create(screen);
  lv_obj_set_pos(personalityPage,0,66);
  lv_obj_set_size(personalityPage,800,356);
  lv_obj_set_style_border_width(personalityPage,0,0);
  lv_obj_set_style_bg_opa(personalityPage,LV_OPA_TRANSP,0);
  lv_obj_set_style_pad_all(personalityPage,0,0);
  lv_obj_clear_flag(personalityPage,LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *p=card(personalityPage,14,10,772,334);

  lv_obj_t *title=lv_label_create(p);
  lv_label_set_text(title,"PLANT PERSONALITY");
  lv_obj_set_style_text_font(title,&lv_font_montserrat_24,0);
  lv_obj_set_style_text_color(title,lv_color_hex(0xE5ECE7),0);
  lv_obj_set_pos(title,18,14);

  lv_obj_t *hint=lv_label_create(p);
  lv_label_set_text(hint,"Choose a phrase style, then tap SAVE.");
  lv_obj_set_style_text_font(hint,&lv_font_montserrat_12,0);
  lv_obj_set_style_text_color(hint,lv_color_hex(0x8DA695),0);
  lv_obj_set_pos(hint,18,47);

  static const char *const names[6]={
    "CLASSIC","FUNNY","SARCASTIC","DRAMATIC","RUDE","MIXED - ALL"
  };
  for (uint8_t i=0;i<6;++i) {
    lv_obj_t *button=lv_btn_create(p);
    themeChoiceButtons[i]=button;
    lv_obj_set_size(button,736,30);
    lv_obj_set_pos(button,18,68+i*34);
    lv_obj_set_style_radius(button,8,0);
    lv_obj_set_style_shadow_width(button,0,0);
    lv_obj_add_event_cb(button,themeChoiceEvent,LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_t *name=lv_label_create(button);
    lv_label_set_text(name,names[i]);
    lv_obj_set_style_text_font(name,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(name,lv_color_hex(0xE5ECE7),0);
    lv_obj_center(name);
  }

  lv_obj_t *cancel=lv_btn_create(p);
  lv_obj_set_size(cancel,350,42);
  lv_obj_set_pos(cancel,18,278);
  lv_obj_set_style_radius(cancel,10,0);
  lv_obj_set_style_bg_color(cancel,lv_color_hex(0x233029),0);
  lv_obj_set_style_border_width(cancel,1,0);
  lv_obj_set_style_border_color(cancel,lv_color_hex(0x405348),0);
  lv_obj_add_event_cb(cancel,themeCancelEvent,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *cancelLabel=lv_label_create(cancel);
  lv_label_set_text(cancelLabel,"CANCEL");
  lv_obj_set_style_text_font(cancelLabel,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_color(cancelLabel,lv_color_hex(0xE5ECE7),0);
  lv_obj_center(cancelLabel);

  lv_obj_t *save=lv_btn_create(p);
  lv_obj_set_size(save,350,42);
  lv_obj_set_pos(save,404,278);
  lv_obj_set_style_radius(save,10,0);
  lv_obj_set_style_bg_color(save,lv_color_hex(0x3F7A4E),0);
  lv_obj_add_event_cb(save,themeSaveEvent,LV_EVENT_CLICKED,nullptr);
  lv_obj_t *saveLabel=lv_label_create(save);
  lv_label_set_text(saveLabel,"SAVE");
  lv_obj_set_style_text_font(saveLabel,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_color(saveLabel,lv_color_hex(0xE5ECE7),0);
  lv_obj_center(saveLabel);

  pendingTheme=phraseTheme;
  refreshThemeDialogSelection();
}

'''
s=s[:builder_start]+page_builder+s[builder_end+1:]

# Build it using the same startup path as the proven Home/Advanced pages.
build_anchor='''  buildSettings(screen);
  buildAdvanced(screen);
  buildNav(screen);
'''
build_repl='''  buildSettings(screen);
  buildAdvanced(screen);
  buildPersonality(screen);
  buildNav(screen);
'''
replace_once(build_anchor,build_repl,"buildUi personality page")

p.write_text(s,encoding="utf-8")
