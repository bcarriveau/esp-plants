from pathlib import Path
import json
import subprocess
import textwrap

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "firmware" / "waveshare-hub" / "include"


def test_moisture_policy_exhaustive(tmp_path):
    source = tmp_path / "test.cpp"
    binary = tmp_path / "test"
    source.write_text(textwrap.dedent(r'''
        #include <cassert>
        #include <cstdint>
        #include "moisture_policy.h"
        using namespace espplants_moisture;

        int main() {
          const uint8_t expected[5][5] = {
            {4, 8, 15, 35, 55},
            {6, 12, 25, 50, 70},
            {10, 20, 40, 70, 85},
            {18, 32, 50, 78, 90},
            {25, 40, 55, 82, 92},
          };

          for (int pref = -2; pref <= 2; ++pref) {
            const auto &t = thresholdsFor(static_cast<int8_t>(pref));
            const uint8_t actual[5] = {t.critical, t.veryDry, t.dry, t.good, t.wet};
            for (int i = 0; i < 5; ++i) assert(actual[i] == expected[pref + 2][i]);
            assert(t.critical < t.veryDry && t.veryDry < t.dry &&
                   t.dry < t.good && t.good < t.wet && t.wet < 100);
            assert(careState(0, pref) == CareState::CRITICAL);
            assert(careState(100, pref) == CareState::VERY_WET);
            for (int m = 0; m <= 100; ++m) {
              auto state = careState(static_cast<uint8_t>(m), static_cast<int8_t>(pref));
              assert(static_cast<int>(state) <= 5);
              auto key = rankingKey(static_cast<uint8_t>(m), static_cast<int8_t>(pref));
              assert(key.band == static_cast<uint8_t>(state));
              assert(key.positionPermille <= 1000);
            }
          }

          // NORMAL must exactly preserve the legacy raw thresholds.
          assert(careState(10, 0) == CareState::CRITICAL);
          assert(careState(11, 0) == CareState::VERY_DRY);
          assert(careState(20, 0) == CareState::VERY_DRY);
          assert(careState(21, 0) == CareState::DRY);
          assert(careState(40, 0) == CareState::DRY);
          assert(careState(41, 0) == CareState::GOOD);
          assert(careState(70, 0) == CareState::GOOD);
          assert(careState(71, 0) == CareState::WET);
          assert(careState(85, 0) == CareState::WET);
          assert(careState(86, 0) == CareState::VERY_WET);

          assert(needsWater(CareState::CRITICAL));
          assert(needsWater(CareState::VERY_DRY));
          assert(!needsWater(CareState::DRY));
          assert(!needsWater(CareState::GOOD));
          assert(!needsWater(CareState::WET));
          assert(!needsWater(CareState::VERY_WET));

          // Preference-aware ranking: the cactus is GOOD while the fern is VERY_DRY.
          auto cactus = rankingKey(18, -2);
          auto fern = rankingKey(38, 2);
          assert(cactus.band == static_cast<uint8_t>(CareState::GOOD));
          assert(fern.band == static_cast<uint8_t>(CareState::VERY_DRY));
          assert(fern.band < cactus.band);
          return 0;
        }
    '''))
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(INCLUDE), str(source), "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)


def test_water_warning_no_longer_drives_care_state():
    main = (ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp").read_text()
    phrase = (ROOT / "firmware" / "waveshare-hub" / "include" / "phrase_engine.h").read_text()
    assert "stateFor(" not in main
    assert "stateFor(" not in phrase
    assert "waterWarning" in main  # retained for diagnostics/logging
    assert "espplants_moisture::needsWater" in main


def test_raw_moisture_display_paths_stay_raw():
    main = (ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp").read_text()
    assert 'snprintf(moisture, sizeof(moisture), "%u%%", sensors[slot].soilMoisturePct)' in main
    assert 'lv_bar_set_value(row.bar, sensors[slot].soilMoisturePct, LV_ANIM_OFF)' in main
    assert 'hasFreshMoisture(home) ? home.soilMoisturePct : 0' in main
    assert 'hasFreshMoisture(s) ? s.soilMoisturePct : 0' in main
    assert 'Serial.printf(" soil=%u%%", report.soilMoisturePct)' in main


def test_preference_persistence_and_plant_detail_geometry():
    main = (ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp").read_text()
    assert 'preferences.putChar(key, sensors[slot].moisturePreference)' in main
    assert 'preferences.getChar(preferenceKey, 0)' in main
    assert 'preferences.remove(key);\n  sensors[slot] = PlantSensor{};' in main
    assert 'replacement.moisturePreference = preservedMoisturePreference;' in main

    def find_identifier(node, identifier):
        if isinstance(node, dict):
            if node.get("identifier") == identifier:
                return node
            for value in node.values():
                found = find_identifier(value, identifier)
                if found is not None:
                    return found
        elif isinstance(node, list):
            for value in node:
                found = find_identifier(value, identifier)
                if found is not None:
                    return found
        return None

    projects = [
        ("esp_plants.eez-project", 772, 334),
        ("esp_plants_7b.eez-project", 988, 418),
    ]
    for project_name, card_width, card_height in projects:
        project = json.loads((ROOT / "firmware" / "waveshare-hub" / project_name).read_text())
        slider = find_identifier(project, "detail_preference_slider")
        value = find_identifier(project, "detail_preference_value")
        save = find_identifier(project, "detail_preference_save_button")
        assert slider is not None and value is not None and save is not None
        for control in (slider, value, save):
            assert control["left"] >= 0 and control["top"] >= 0
            assert control["left"] + control["width"] <= card_width
            assert control["top"] + control["height"] <= card_height

    # Slider movement is only a pending UI change; persistence happens in SAVE.
    slider_event = main.split("void moisturePreferenceEvent", 1)[1].split(
        "void saveMoisturePreferenceEvent", 1
    )[0]
    save_event = main.split("void saveMoisturePreferenceEvent", 1)[1].split(
        "void buildPlant", 1
    )[0]
    assert "saveMoisturePreference(" not in slider_event
    assert "sensor.moisturePreference =" not in slider_event
    assert "saveMoisturePreference(" in save_event
    assert "sensor.moisturePreference = next;" in save_event


def test_preference_change_does_not_fake_phrase_advance():
    main = (ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp").read_text()
    event = main.split("void moisturePreferenceEvent", 1)[1].split("void buildPlant", 1)[0]
    assert "espplants_phrases::select" in event
    assert "state, seed, false" in event
    report = main.split("void handleSensorReport", 1)[1].split("void clearRoutes", 1)[0]
    assert "if (moistureReported)" in report
    assert "state,seed,true" in report.replace(" ", "")
