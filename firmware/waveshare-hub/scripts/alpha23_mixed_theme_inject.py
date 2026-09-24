Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.22 partial-report merge and phrase engine" not in s:
    raise RuntimeError("alpha.23 requires current alpha.22-or-newer source")

if "Alpha.23 persistent phrase theme selector" in s:
    Return()

def replace_once(old,new,what):
    global s
    if old not in s:
        raise RuntimeError("alpha.23 anchor missing: "+what)
    s=s.replace(old,new,1)

replace_once(
    "bool useFahrenheit = true;\n",
    '''bool useFahrenheit = true;
// Alpha.23 persistent phrase theme selector
espplants_phrases::Theme phraseTheme = espplants_phrases::Theme::MIXED;
''',
    "settings globals")

replace_once(
    "lv_obj_t *settingsUnit = nullptr;\n",
    '''lv_obj_t *settingsUnit = nullptr;
lv_obj_t *settingsTheme = nullptr;
''',
    "settings theme label")

start=s.find("void unitEvent(lv_event_t *event) {")
if start<0:
    raise RuntimeError("alpha.23 anchor missing: unitEvent")
end=s.find("\n}\n",start)
if end<0:
    raise RuntimeError("alpha.23 anchor missing: unitEvent end")
end+=3
theme_event=r'''
void themeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  uint8_t next=static_cast<uint8_t>(phraseTheme)+1u;
  if (next>static_cast<uint8_t>(espplants_phrases::Theme::MIXED)) next=0;
  phraseTheme=static_cast<espplants_phrases::Theme>(next);
  preferences.putUChar("phrase_theme",next);
  for (auto &sensor:sensors) sensor.phraseRotation=espplants_phrases::Rotation{};
  Serial.printf("[settings] phrase theme=%s\n",
                espplants_phrases::themeName(phraseTheme));
  uiDirty=true;
}
'''
s=s[:end]+theme_event+s[end:]

replace_once(
    "espplants_phrases::Theme::FUNNY,state,seed,false",
    "phraseTheme,state,seed,false",
    "mood theme")

old_advance='''    const bool warning=(report.fieldFlags & plantlink::SensorHasWaterWarning) && report.waterWarning;
    const auto state=espplants_phrases::stateFor(report.soilMoisturePct,warning);
    const size_t phraseSlot=static_cast<size_t>(s-sensors);
    const uint32_t seed=static_cast<uint32_t>(phraseSlot*2654435761u)^static_cast<uint32_t>(report.soilMoisturePct*257u);
    (void)espplants_phrases::select(s->phraseRotation,espplants_phrases::Theme::FUNNY,state,seed,true);
'''
new_advance='''    // Alpha.23: ONLY a soil-moisture report advances/selects a phrase.
    // Temperature, humidity, battery, LQI/signal, etc. never touch it.
    const bool warning=(s->reportedFieldFlagsThisBoot & plantlink::SensorHasWaterWarning) && s->waterWarning;
    const auto state=espplants_phrases::stateFor(report.soilMoisturePct,warning);
    const size_t phraseSlot=static_cast<size_t>(s-sensors);
    const uint32_t seed=static_cast<uint32_t>(phraseSlot*2654435761u)^
                        static_cast<uint32_t>(report.soilMoisturePct*257u)^millis();
    (void)espplants_phrases::select(s->phraseRotation,phraseTheme,state,seed,true);
'''
replace_once(old_advance,new_advance,"moisture phrase block")

ui_anchor='''  settingsUnit = lv_label_create(unitButton);
  lv_obj_set_style_text_font(settingsUnit, &lv_font_montserrat_24, 0);
  lv_obj_center(settingsUnit);

  cap = lv_label_create(setup);
'''
ui_repl='''  settingsUnit = lv_label_create(unitButton);
  lv_obj_set_style_text_font(settingsUnit, &lv_font_montserrat_24, 0);
  lv_obj_center(settingsUnit);

  lv_obj_t *themeButton = lv_btn_create(setup);
  lv_obj_set_size(themeButton, 120, 46);
  lv_obj_set_pos(themeButton, 112, 56);
  lv_obj_set_style_radius(themeButton, 12, 0);
  lv_obj_set_style_bg_color(themeButton, lv_color_hex(0x244F39), 0);
  lv_obj_add_event_cb(themeButton, themeEvent, LV_EVENT_CLICKED, nullptr);
  settingsTheme = lv_label_create(themeButton);
  lv_obj_set_style_text_font(settingsTheme, &lv_font_montserrat_12, 0);
  lv_obj_set_width(settingsTheme, 104);
  lv_obj_set_style_text_align(settingsTheme, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(settingsTheme, LV_LABEL_LONG_DOT);
  lv_obj_center(settingsTheme);

  cap = lv_label_create(setup);
'''
replace_once(ui_anchor,ui_repl,"settings UI")
replace_once('lv_label_set_text(cap, "DISPLAY TEMPERATURE");',
             'lv_label_set_text(cap, "PERSONALITY                 TEMP");',
             "settings caption")

replace_once(
    '''  label(settingsUnit, useFahrenheit ? "F" : "C");
  if (permitJoinRemaining)''',
    '''  label(settingsUnit, useFahrenheit ? "F" : "C");
  label(settingsTheme, espplants_phrases::themeName(phraseTheme));
  if (permitJoinRemaining)''',
    "settings refresh")

replace_once(
    '''  useFahrenheit = preferences.getBool("fahrenheit", true);
  String savedDeviceName''',
    '''  useFahrenheit = preferences.getBool("fahrenheit", true);
  {
    const uint8_t savedTheme=preferences.getUChar(
        "phrase_theme", static_cast<uint8_t>(espplants_phrases::Theme::MIXED));
    phraseTheme=static_cast<espplants_phrases::Theme>(
        savedTheme<=static_cast<uint8_t>(espplants_phrases::Theme::MIXED)
            ? savedTheme : static_cast<uint8_t>(espplants_phrases::Theme::MIXED));
  }
  String savedDeviceName''',
    "theme preference load")

p.write_text(s,encoding="utf-8")
