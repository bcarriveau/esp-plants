Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.26 personality settings page" not in s:
    raise RuntimeError("alpha.27 requires alpha.26 source")

if "Alpha.27 zero-allocation personality selector" in s:
    Return()

def replace_once(old,new,what):
    global s
    if old not in s:
        raise RuntimeError("alpha.27 anchor missing: "+what)
    s=s.replace(old,new,1)

s=s.replace(
    "// Alpha.26 personality settings page\n",
    "// Alpha.26 personality settings page\n"
    "// Alpha.27 zero-allocation personality selector\n",
    1)

# Remove the extra personality page/object tree. The crash dump showed a null
# lv_obj pointer inside the LVGL task. Reuse the already-existing rename modal
# and btnmatrix instead of allocating any new LVGL objects.
replace_once(
    "enum class Page : uint8_t { Home = 0, All = 1, Plant = 2, Settings = 3, Advanced = 4, Personality = 5 };",
    "enum class Page : uint8_t { Home = 0, All = 1, Plant = 2, Settings = 3, Advanced = 4 };",
    "Page enum")

s=s.replace("lv_obj_t *personalityPage = nullptr;\n","",1)
s=s.replace("lv_obj_t *themeChoiceButtons[6]{};\n","",1)

show_block='''  if (personalityPage) (page == Page::Personality) ? lv_obj_clear_flag(personalityPage, LV_OBJ_FLAG_HIDDEN)
                                                       : lv_obj_add_flag(personalityPage, LV_OBJ_FLAG_HIDDEN);
'''
if show_block not in s:
    raise RuntimeError("alpha.27 anchor missing: personality showPage block")
s=s.replace(show_block,"",1)

replace_once(
'''  if (navSettings) lv_obj_set_style_bg_color(
      navSettings,
      (page == Page::Settings || page == Page::Personality) ? active : idle, 0);
''',
'''  if (navSettings) lv_obj_set_style_bg_color(navSettings, page == Page::Settings ? active : idle, 0);
''',
"settings nav highlight")

# Replace all alpha.24-26 personality callbacks with one open handler that
# repurposes objects that already exist in buildRename().
start=s.find("void refreshThemeDialogSelection() {")
end=s.find("void startPairing(bool replacing, int targetSlot);",start)
if start<0 or end<0:
    raise RuntimeError("alpha.27 anchor missing: personality callback block")

theme_block=r'''static const char *kPersonalityMap[] = {
    "CLASSIC", "\n",
    "FUNNY", "\n",
    "SARCASTIC", "\n",
    "DRAMATIC", "\n",
    "RUDE", "\n",
    "MIXED - ALL", "\n",
    "CANCEL", "SAVE", ""
};

bool personalityDialogActive = false;

void themeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!renameModal || !renameKeyboard || !renameInput) return;

  pendingTheme=phraseTheme;
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
  lv_obj_clear_flag(renameModal,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(renameModal);
}

'''
s=s[:start]+theme_block+s[end:]

# Remove the alpha.26 personality page builder completely.
builder_start=s.find("void buildPersonality(lv_obj_t *screen) {")
builder_end=s.find("\nvoid buildPairDialog(lv_obj_t *screen) {",builder_start)
if builder_start<0 or builder_end<0:
    raise RuntimeError("alpha.27 anchor missing: buildPersonality")
s=s[:builder_start]+s[builder_end+1:]

s=s.replace("  buildPersonality(screen);\n","",1)

# Restore the rename modal geometry whenever it closes after personality use.
replace_once(
'''void closeRename() {
  lv_obj_add_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
}
''',
'''void closeRename() {
  if (personalityDialogActive) {
    personalityDialogActive=false;
    lv_obj_clear_flag(renameInput,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(renameKeyboard,18,158);
    lv_obj_set_size(renameKeyboard,764,304);
  }
  lv_obj_add_flag(renameModal, LV_OBJ_FLAG_HIDDEN);
}
''',
"closeRename restore")

# Route the existing btnmatrix callback to personality selection while that
# dialog mode is active. SAVE is the only action that writes NVS.
key_anchor='''  const uint16_t id = lv_btnmatrix_get_selected_btn(renameKeyboard);
  const char *key = lv_btnmatrix_get_btn_text(renameKeyboard, id);
  if (!key) return;

'''
if key_anchor not in s:
    raise RuntimeError("alpha.27 anchor missing: rename keyboard key read")

personality_route=r'''  const uint16_t id = lv_btnmatrix_get_selected_btn(renameKeyboard);
  const char *key = lv_btnmatrix_get_btn_text(renameKeyboard, id);
  if (!key) return;

  if (personalityDialogActive) {
    if (strcmp(key,"SAVE")==0) {
      if (pendingTheme!=phraseTheme) {
        phraseTheme=pendingTheme;
        preferences.putUChar("phrase_theme",static_cast<uint8_t>(phraseTheme));
        for (auto &sensor:sensors) sensor.phraseRotation=espplants_phrases::Rotation{};
        Serial.printf("[settings] phrase theme=%s\n",
                      espplants_phrases::themeName(phraseTheme));
      }
      uiDirty=true;
      closeRename();
      return;
    }

    if (strcmp(key,"CANCEL")==0) {
      pendingTheme=phraseTheme;
      closeRename();
      return;
    }

    if (strcmp(key,"CLASSIC")==0) pendingTheme=espplants_phrases::Theme::CLASSIC;
    else if (strcmp(key,"FUNNY")==0) pendingTheme=espplants_phrases::Theme::FUNNY;
    else if (strcmp(key,"SARCASTIC")==0) pendingTheme=espplants_phrases::Theme::SARCASTIC;
    else if (strcmp(key,"DRAMATIC")==0) pendingTheme=espplants_phrases::Theme::DRAMATIC;
    else if (strcmp(key,"RUDE")==0) pendingTheme=espplants_phrases::Theme::RUDE;
    else if (strcmp(key,"MIXED - ALL")==0) pendingTheme=espplants_phrases::Theme::MIXED;
    else return;

    char hint[96]{};
    snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
             espplants_phrases::themeName(pendingTheme));
    label(renameHint,hint);
    return;
  }

'''
s=s.replace(key_anchor,personality_route,1)

p.write_text(s,encoding="utf-8")
