from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS_MAIN = ROOT / "firmware/waveshare-hub/src/main.cpp"
H2_MAIN = ROOT / "firmware/m5-h2-zigbee/src/main.cpp"
H2_OTA = ROOT / "firmware/waveshare-hub/src/h2_ota_client.cpp"
PLANTLINK = ROOT / "shared/plantlink/plantlink.h"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_pairing_activity_is_backward_compatible_and_does_not_register_sensor():
    protocol = read(PLANTLINK)
    h2 = read(H2_MAIN)
    ws = read(WS_MAIN)
    assert "PairingActivity=0x16" in protocol
    assert "sendFrame(plantlink::MessageType::PairingActivity" in h2
    assert "neighbor.device_type != ESP_ZB_DEVICE_TYPE_ED" in h2
    assert "neighbor.relationship != ESP_ZB_NWK_RELATIONSHIP_CHILD" in h2
    assert "findSensorByIeee(neighbor.ieee_addr)" in h2
    activity = h2.split("void sendPairingActivity", 1)[1].split("void sendDeviceLeft", 1)[0]
    assert "findOrCreateSensor" not in activity
    assert "sendDeviceJoined" not in activity
    assert "case plantlink::MessageType::PairingActivity: handlePairingActivity(frame);" in ws


def test_waveshare_acknowledges_join_before_strict_zg303z_confirmation():
    ws = read(WS_MAIN)
    h2 = read(H2_MAIN)
    assert "Verifying = 5" in ws
    assert '"Zigbee device joined. Confirming it is a supported plant sensor..."' in ws
    assert '"VERIFYING SENSOR..."' in ws
    assert "pairDialogState != PairDialogState::Verifying" in ws
    confirmed = h2.split("if (!sensor && decoded && zg303zEvidence)", 1)[1].split(
        "if (sensor && decoded)", 1
    )[0]
    assert "findOrCreateSensor(event, created)" in confirmed
    assert "sendDeviceJoined(*sensor);" in confirmed


def test_replacement_gets_explicit_success_acknowledgement():
    ws = read(WS_MAIN)
    assert 'pairReplacing ? "SENSOR REPLACED" : "SENSOR FOUND"' in ws
    assert '"%s now uses the new sensor."' in ws
    assert 'label(pairStatus, "Replacement complete.");' in ws
    assert "lv_obj_add_flag(pairPrimary, LV_OBJ_FLAG_HIDDEN);" in ws


def test_h2_identity_probe_retries_and_uses_only_fresh_positive_cache():
    ota = read(H2_OTA)
    assert "kIdentityProbeTimeoutMs = 3200" in ota
    assert "kIdentityProbeRetryMs = 850" in ota
    assert "kKnownIdentityFreshMs = 10000" in ota
    probe = ota.split("TargetState probeTarget", 1)[1].split("struct AssetHeaderState", 1)[0]
    assert "now - lastProbeMs >= kIdentityProbeRetryMs" in probe
    assert "millis() - cachedAt <= kKnownIdentityFreshMs" in probe
    assert "classifyTargetHello(cached, release)" in probe
    assert "clearLiveIdentity()" in ota


def test_link_loss_and_h2_reboot_invalidate_cached_identity():
    ws = read(WS_MAIN)
    heartbeat = ws.split("case plantlink::MessageType::Heartbeat:", 1)[1].split(
        "case plantlink::MessageType::HelloAck", 1
    )[0]
    assert "h2BuildId[0] = '\\0';" in heartbeat
    assert "espplants_h2_ota::clearLiveIdentity();" in heartbeat
    service = ws.split("void servicePlantLink()", 1)[1].split("}  // namespace", 1)[0]
    assert "h2BuildId[0] = '\\0';" in service
    assert "espplants_h2_ota::clearLiveIdentity();" in service


def test_versions_advance_for_both_changed_firmwares():
    ws_header = read(ROOT / "firmware/waveshare-hub/include/build_version.h")
    h2_header = read(ROOT / "firmware/m5-h2-zigbee/include/build_version.h")
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.60"' in ws_header
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.32"' in h2_header
    assert read(ROOT / "VERSION").strip() == "0.2.0-alpha.60"
