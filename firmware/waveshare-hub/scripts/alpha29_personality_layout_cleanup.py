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

def require(what,needle):
    if needle not in s:
        raise RuntimeError("alpha.29 unexpected source near: "+what)

if "Alpha.29 personality layout cleanup" not in s:
    s=s.replace(
        "// Alpha.28 personality selection polish\n",
        "// Alpha.28 personality selection polish\n"
        "// Alpha.29 personality layout cleanup\n",1)

replace_if_present(
'''  pendingTheme=phraseTheme;
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
''',
'''  pendingTheme=phraseTheme;
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
''')

# restore width only if not already present
restore='''    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_pos(renameKeyboard,18,158);
'''
if restore in s:
    s=s.replace(restore,'''    lv_obj_set_pos(renameTitle,18,8);
    lv_obj_set_pos(renameHint,20,36);
    lv_obj_set_width(renameHint,520);
    lv_obj_set_pos(renameKeyboard,18,158);
''',1)

replace_if_present(
'''    snprintf(hint,sizeof(hint),"Selected: %s. Tap SAVE to apply.",
             espplants_phrases::themeName(pendingTheme));
''',
'''    snprintf(hint,sizeof(hint),"Selected: %s. SAVE applies it.",
             espplants_phrases::themeName(pendingTheme));
''')

replace_if_present(
'''  lv_obj_set_pos(cap, 112, 50);
''',
'''  lv_obj_set_pos(cap, 127, 44);
''')
replace_if_present(
'''  lv_obj_set_pos(tempCap, 242, 50);
''',
'''  lv_obj_set_pos(tempCap, 247, 44);
''')
replace_if_present(
'''  lv_obj_set_pos(unitButton, 242, 66);
''',
'''  lv_obj_set_pos(unitButton, 242, 62);
''')
replace_if_present(
'''  lv_obj_set_pos(themeButton, 112, 66);
''',
'''  lv_obj_set_pos(themeButton, 112, 62);
''')
replace_if_present(
'''  label(settingsUnit, useFahrenheit ? "TEMP °F" : "TEMP °C");
  snprintf(text,sizeof(text),"%s  >",espplants_phrases::themeName(phraseTheme));
  label(settingsTheme,text);
''',
'''  label(settingsUnit, useFahrenheit ? "°F" : "°C");
  label(settingsTheme, espplants_phrases::themeName(phraseTheme));
''')

require("theme button","lv_obj_set_pos(themeButton, 112, 62);")
require("temp button","lv_obj_set_pos(unitButton, 242, 62);")

p.write_text(s,encoding="utf-8")
