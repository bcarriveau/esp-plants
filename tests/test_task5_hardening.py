from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_h2_reboot_does_not_auto_open_commissioning():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")
    assert "Zigbee.setRebootOpenNetwork(0);" in main
    assert "Zigbee.openNetwork(30)" not in main
    assert "one-shot router rejoin window" not in main
    assert "case plantlink::MessageType::PermitJoin" in main
    assert "frame.payloadLength != 1" in main
    assert "Zigbee.openNetwork(seconds);" in main


def test_explicit_add_sensor_path_still_exists_on_waveshare():
    # The UI/source itself is intentionally unchanged by Task 5. This guard is
    # evaluated against the checked-out repo when this test is copied into it.
    path = ROOT / "firmware" / "waveshare-hub" / "src" / "main.cpp"
    assert path.exists(), "run this test from a full ESP PLANTS checkout"
    main = path.read_text(encoding="utf-8")
    assert "void requestJoin(uint8_t seconds)" in main
    assert "sendFrame(plantlink::MessageType::PermitJoin, &seconds, 1);" in main
    assert "requestJoin(120);" in main
    assert 'pairReplacing ? "REPLACE SENSOR" : "ADD SENSOR"' in main


def test_h2_release_debug_surface_is_reduced_but_dev_mode_remains():
    pio = read("firmware/m5-h2-zigbee/platformio.ini")
    main = read("firmware/m5-h2-zigbee/src/main.cpp")

    assert "-DESP_PLANTS_H2_DEV_DIAGNOSTICS=1" in pio
    release = pio.split("[env:m5_gateway_h2_release]", 1)[1]
    assert "-DCORE_DEBUG_LEVEL=0" in release
    assert "-DESP_PLANTS_DISTRIBUTION_BUILD=1" in release
    assert "ESP_PLANTS_H2_DEV_DIAGNOSTICS" not in release

    assert "#if defined(ESP_PLANTS_H2_DEV_DIAGNOSTICS)" in main
    assert "Zigbee.setDebugMode(true);" in main
    assert "Zigbee.setDebugMode(false);" in main
    assert "pairing command disabled in distribution build" in main


def test_privileged_commands_have_narrow_guards():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")
    assert "ignored unconfirmed factory reset command" in main
    assert "rejected remove for ieee=%s because no joined/known short address exists" in main
    assert "if (frame.payloadLength == 8)" in main


def test_h2_ota_requires_fresh_controller_session_and_keeps_validation():
    header = read("firmware/m5-h2-zigbee/include/h2_ota_receiver.h")
    receiver = read("firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp")
    client = read("firmware/waveshare-hub/src/h2_ota_client.cpp")
    ota = read("shared/plantlink/plantlink_ota.h")

    assert "void noteControllerHello(const plantlink::Frame &frame);" in header
    assert "kOtaAuthorizationWindowMs=5000u" in receiver
    assert "consumeOtaAuthorization" in receiver
    assert "plantlink_ota::Error::Unauthorized" in receiver
    assert "Unauthorized" in ota
    assert "kAuthorizeIntentMagic" in ota
    assert "f.sequence==authorizedBeginSequence" in receiver
    assert "plantlink::getU32LE(f.payload+4)==plantlink_ota::kAuthorizeIntentMagic" in receiver
    assert "kExpectedDistributionMarker[]=ESP_PLANTS_H2_RELEASE_MARKER" in receiver

    # Existing inactive-slot, chip/image, digest and embedded identity validation
    # must remain intact.
    for token in (
        "esp_ota_get_next_update_partition",
        "esp_ota_get_running_partition",
        "incoming.magic!=ESP_IMAGE_HEADER_MAGIC",
        "incoming.chip_id!=running.chip_id",
        "memcmp(digest,ota.expectedSha,32)!=0",
        "!ota.buildSeen||!ota.markerSeen",
        "esp_ota_end",
        "esp_ota_set_boot_partition",
    ):
        assert token in receiver

    # The Waveshare explicitly arms OTA immediately before Begin, after the
    # HTTPS download/SHA validation may have consumed significant time.
    sha_check = client.index("H2 firmware SHA-256 mismatch")
    intent = client.index("kAuthorizeIntentMagic", sha_check)
    hello = client.index("sendFrame(plantlink::MessageType::Hello", intent)
    begin = client.index("sendFrame(plantlink::MessageType::H2OtaBegin", hello)
    assert sha_check < intent < hello < begin

    # H2 and Waveshare versions are independent. Never derive H2 asset/build
    # names from release.buildId / the Waveshare alpha number.
    assert '#include "../../m5-h2-zigbee/include/build_version.h"' in client
    assert "ESP_PLANTS_H2_BUILD_ID" in client
    assert "ESP_PLANTS_H2_VERSION" in client
    assert "kMaxH2Bytes=0xE0000u" in client
    assert "release.buildId+" not in client


def test_distribution_crash_diagnostics_log_instead_of_forced_panic():
    pio = read("firmware/waveshare-hub/platformio.ini")
    diag = read("firmware/waveshare-hub/src/crash_diagnostics.cpp")

    assert "upload_port = COM11" in pio
    assert "[env:waveshare_s3_touch_lcd_7_diag]" in pio
    assert "-DESP_PLANTS_DIAGNOSTIC_PANIC=1" in pio
    assert "#if defined(ESP_PLANTS_DIAGNOSTIC_PANIC)" in diag
    assert "automatic diagnostic panic disabled" in diag
    assert "abort();" in diag
    assert "phase != MainPhase::UpdateService" in diag
