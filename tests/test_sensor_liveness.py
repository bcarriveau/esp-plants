from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_liveness_is_ram_only_and_filters_live_home_data():
    main = read("firmware/waveshare-hub/src/main.cpp")
    header = read("firmware/waveshare-hub/include/sensor_liveness.h")

    assert "espplants_sensor_liveness::Tracker liveness{};  // RAM-only; never persisted." in main
    assert "return sensor.used && sensor.seenThisBoot && !sensor.liveness.stale;" in main
    assert "return isSensorReporting(sensor) && hasReportedMoistureThisBoot(sensor);" in main
    assert "if (!isSensorStale(sensors[slot])) logicalSlots[logicalCount++] = slot;" in main
    assert "serviceSensorLiveness();" in main
    assert "noteReport(s->liveness, s->lastSeenMs, reportNowMs);" in main
    assert "NEEDS ATTENTION" not in main  # UI uses compact ATTENTION summary/status wording.
    assert "%u REPORTING | %u WAITING | %u ATTENTION" in main

    assert "kBootstrapTimeoutMs = 2UL * 60UL * 60UL * 1000UL" in header
    assert "kMinimumTimeoutMs = 15UL * 60UL * 1000UL" in header
    assert "kMaximumTimeoutMs = 3UL * 60UL * 60UL * 1000UL" in header
    assert "kMissedPeriodsBeforeStale = 3" in header
    assert "kCadenceSamplesRequired = 3" in header


def test_stale_rows_keep_last_known_moisture_but_not_live_watering_status():
    main = read("firmware/waveshare-hub/src/main.cpp")
    assert "if (hasReportedMoistureThisBoot(sensors[slot]))" in main
    assert 'if (isSensorStale(sensor)) snprintf(out, size, "ATTN %s", age);' in main
    assert "const bool thirsty = hasFreshMoisture(sensors[slot]) && lastKnownThirsty;" in main
    assert 'return "Needs attention - sensor not reporting";' in main


def test_home_surfaces_stale_sensor_attention_without_using_stale_data_as_live():
    source = (ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp").read_text()
    assert "homeAttentionHint = objects.sensor_details_hint" in source
    assert '"ATTN: %.14s | LAST %u%%%s | %s AGO"' in source
    assert 'dryAttention ? " DRY" : ""' in source
    assert "const int attentionSensor = mostConcerningStaleSensor();" in source
