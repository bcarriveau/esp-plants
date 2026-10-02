from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp").read_text()


def map_body(name: str) -> str:
    start = MAIN.index(f"static const char *{name}[] = {{")
    end = MAIN.index("};", start)
    return MAIN[start:end]


def test_rename_keyboards_include_digits_in_both_cases():
    expected = '"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\\n"'
    assert expected in map_body("kRenameUpperMap")
    assert expected in map_body("kRenameLowerMap")


def test_single_character_keys_still_enter_directly():
    start = MAIN.index("void renameKeyboardEvent(")
    end = MAIN.index("void openPlantRename(", start)
    body = MAIN[start:end]
    assert "strlen(key) == 1" in body
    assert 'lv_textarea_add_text(renameInput, key);' in body


def test_keyboard_geometry_stays_inside_fullscreen_modals():
    # Rename keyboard: original 800x480 geometry is y=158, h=304 => 462.
    # The 7B runtime scale is 1.25 in Y => y=198, h=380 => 578 of 600.
    assert "lv_obj_set_pos(renameKeyboard, uiX(18), uiY(158));" in MAIN
    assert "lv_obj_set_size(renameKeyboard, uiX(764), uiY(304));" in MAIN
    assert 158 + 304 <= 480
    assert round(158 * 1.25) + round(304 * 1.25) <= 600
