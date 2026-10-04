from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
H2_MAIN = ROOT / "firmware/m5-h2-zigbee/src/main.cpp"
H2_VERSION = ROOT / "firmware/m5-h2-zigbee/include/build_version.h"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_receive_diagnostics_cover_each_failure_boundary():
    main = read(H2_MAIN)

    assert "constexpr size_t kApsQueueDepth = 32;" in main
    assert "apsCallbacks" in main
    assert "apsQueueDrops" in main
    assert "ieeeLookupFailures" in main
    assert "zeroIeeeEvents" in main
    assert "ieeeLateRecoveries" in main
    assert "decodedFrames" in main
    assert "sensorReportsSent" in main
    assert "plantLinkShortWrites" in main
    assert "IEEE unresolved" in main
    assert "IEEE recovered" in main
    assert "PlantUart.write(encoded, length)" in main
    assert "written != length" in main
    assert "printReceiveDiagnostics" in main


def test_network_identity_is_logged_and_compared_in_ordinary_nvs():
    main = read(H2_MAIN)

    assert "#include <Preferences.h>" in main
    assert "esp_zb_bdb_is_factory_new()" in main
    assert "esp_zb_get_pan_id()" in main
    assert "esp_zb_get_extended_pan_id(extendedPan)" in main
    assert "currentChannel()" in main
    assert 'constexpr char kNetworkFingerprintNamespace[] = "espplants-h2";' in main
    assert 'constexpr char kNetworkFingerprintKey[] = "zbfp";' in main
    assert "prefs.putBytes(kNetworkFingerprintKey" in main
    assert "coordinator network fingerprint changed; stored fingerprint retained" in main
    assert "esp_zb_get_tx_power(&txPowerDbm)" in main
    assert "ESP PLANTS does not set TX power" in main
    assert "esp_zb_set_tx_power" not in main


def test_manual_neighbor_dump_is_read_only_and_includes_aging_data():
    main = read(H2_MAIN)

    assert "void printNeighborTable()" in main
    assert "device_timeout=%lu" in main
    assert "timeout_counter=%lu" in main
    assert "relationship=%s(%u)" in main
    assert "lqi=%u rssi=%d" in main
    assert "case 't':" in main
    assert "printNeighborTable();" in main

    dump = main.split("void printNeighborTable()", 1)[1].split(
        "void printNetworkFingerprint", 1
    )[0]
    assert "openNetwork" not in dump
    assert "closeNetwork" not in dump
    assert "factoryReset" not in dump
    assert "esp_zb_zdo_device_leave_req" not in dump


def test_release_does_not_emit_duplicate_raw_tuya_or_hex_spam():
    main = read(H2_MAIN)

    aps_log = main.split("void processApsEvent", 1)[1].split(
        "// Routers/repeaters are infrastructure", 1
    )[0]
    assert "#if defined(ESP_PLANTS_H2_DEV_DIAGNOSTICS)" in aps_log
    assert 'Serial.printf("[aps]' in aps_log

    tuya = main.split("zg303z::decodeTuyaFrame", 1)[1].split(
        "// Hardware-verified HOBEIAN ZG-303Z evidence", 1
    )[0]
    assert "#if defined(ESP_PLANTS_H2_DEV_DIAGNOSTICS)" in tuya
    assert 'Serial.printf("[tuya]' in tuya

    raw = main.split("// Keep unknown/undecoded traffic observable", 1)[1].split(
        "bool apsDataHandler", 1
    )[0]
    assert "#if defined(ESP_PLANTS_H2_DEV_DIAGNOSTICS)" in raw
    assert "event.clusterId == zg303z::kTuyaClusterId" in raw
    release = raw.split("#else", 1)[1].split("#endif", 1)[0]
    assert "event.clusterId == zg303z::kTuyaClusterId" not in release
    assert "if (!sensor || !decoded)" in release


def test_zero_ieee_gets_one_safe_late_resolution_attempt():
    main = read(H2_MAIN)
    block = main.split("bool recoverEventIeee", 1)[1].split(
        "#if defined(ESP_PLANTS_H2_DEV_DIAGNOSTICS)", 1
    )[0]
    assert "esp_zb_ieee_address_by_short(event.shortAddress, resolved)" in block
    assert "neighbor.short_addr == event.shortAddress" in block
    assert "ieeeLateRecoveries" in block
    assert "openNetwork" not in block
    assert "closeNetwork" not in block


def test_stale_infrastructure_short_address_cannot_steal_identified_sensor_packet():
    main = read(H2_MAIN)
    block = main.split("InfrastructureState *router = findInfrastructureByIeee", 1)[1].split(
        "if (router)", 1
    )[0]

    assert "findInfrastructureByShort(event.shortAddress)" in block
    assert "findInfrastructureByShort(event.shortAddress)" in block
    assert "router = shortMatch" not in block
    assert "infrastructureShortRejects" in block


def test_no_rejoin_or_boot_commissioning_policy_change_was_added():
    main = read(H2_MAIN)

    assert "Zigbee.setRebootOpenNetwork(0);" in main
    assert "Zigbee.openNetwork(30)" not in main
    assert "one-shot router rejoin window" not in main
    assert "esp_zb_factory_reset" not in main
    assert "persist" not in main.split("struct SensorState", 1)[1].split(
        "struct InfrastructureState", 1
    )[0].lower()


def test_h2_version_bumped_for_receive_diagnostics():
    version = read(H2_VERSION)
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.33"' in version
