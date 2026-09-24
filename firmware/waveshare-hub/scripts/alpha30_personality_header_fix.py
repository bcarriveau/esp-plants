Import("env")
from pathlib import Path

p=Path(env["PROJECT_DIR"])/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")

if "Alpha.29 personality layout cleanup" not in s:
    raise RuntimeError("alpha.30 requires alpha.29 source")

if "Alpha.30 clear personality header" in s:
    Return()

s=s.replace(
    "// Alpha.29 personality layout cleanup\n",
    "// Alpha.29 personality layout cleanup\n"
    "// Alpha.30 clear personality header\n",1)

# Header: restore compact positions fully inside the fixed 68px bar.
s=s.replace("lv_obj_set_pos(renameTitle,36,18);",
            "lv_obj_set_pos(renameTitle,24,8);",1)
s=s.replace("lv_obj_set_pos(renameHint,36,50);",
            "lv_obj_set_pos(renameHint,24,38);",1)
s=s.replace("lv_obj_set_width(renameHint,620);",
            "lv_obj_set_width(renameHint,700);",1)

# Critical fix: move the list DOWN away from the header, and shorten it so it
# still fits comfortably above the bottom of the 480px display.
s=s.replace("lv_obj_set_pos(renameKeyboard,36,94);",
            "lv_obj_set_pos(renameKeyboard,36,118);",1)
s=s.replace("lv_obj_set_size(renameKeyboard,728,336);",
            "lv_obj_set_size(renameKeyboard,728,300);",1)

if "lv_obj_set_pos(renameKeyboard,36,118);" not in s:
    raise RuntimeError("alpha.30 could not apply selector Y position")
if "lv_obj_set_size(renameKeyboard,728,300);" not in s:
    raise RuntimeError("alpha.30 could not apply selector size")

p.write_text(s,encoding="utf-8")
