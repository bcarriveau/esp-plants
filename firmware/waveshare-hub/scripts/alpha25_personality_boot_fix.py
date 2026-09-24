Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.24 personality picker modal" not in s:
    raise RuntimeError("alpha.25 requires alpha.24 source")

if "Alpha.25 lazy personality modal startup guard" in s:
    Return()

# Mark this source so repeated builds are a no-op.
s=s.replace(
    "// Alpha.24 personality picker modal\n",
    "// Alpha.24 personality picker modal\n// Alpha.25 lazy personality modal startup guard\n",
    1)

# Do not build the personality modal during initial LVGL construction.
# The rest of the proven boot UI now builds exactly as before.
startup_call="  buildThemeDialog(screen);\n"
if startup_call not in s:
    raise RuntimeError("alpha.25 anchor missing: startup theme dialog call")
s=s.replace(startup_call,"",1)

# Forward declare the lazy builder before themeEvent.
anchor="void refreshThemeDialogSelection() {\n"
if anchor not in s:
    raise RuntimeError("alpha.25 anchor missing: theme selection refresh")
s=s.replace(anchor,
            "void buildThemeDialog(lv_obj_t *screen);\n\n"+anchor,1)

# Build the list only when PERSONALITY is actually tapped.
old='''void themeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !themeModal) return;
  pendingTheme=phraseTheme;
'''
new='''void themeEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!themeModal) buildThemeDialog(lv_scr_act());
  if (!themeModal) return;
  pendingTheme=phraseTheme;
'''
if old not in s:
    raise RuntimeError("alpha.25 anchor missing: themeEvent")
s=s.replace(old,new,1)

p.write_text(s,encoding="utf-8")
